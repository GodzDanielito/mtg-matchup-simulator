#include "esecuzioni.hpp"

#include "types.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>

namespace mtg {

namespace {

// --- MD5 (RFC 1321) ---------------------------------------------------------------------------------------------

constexpr std::array<std::uint32_t, 64> kMd5K = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
constexpr std::array<int, 16> kMd5S = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};

std::uint32_t rotl(std::uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }

class Md5 {
public:
    void update(const unsigned char* data, std::size_t n) {
        length_ += n;
        while (n > 0) {
            const std::size_t take = std::min(n, buffer_.size() - used_);
            std::copy(data, data + take, buffer_.begin() + static_cast<std::ptrdiff_t>(used_));
            used_ += take;
            data += take;
            n -= take;
            if (used_ == buffer_.size()) {
                block(buffer_.data());
                used_ = 0;
            }
        }
    }
    std::string hex() {
        const std::uint64_t bits = length_ * 8;
        const unsigned char one = 0x80, zero = 0;
        update(&one, 1);
        while (used_ != 56) update(&zero, 1);
        std::array<unsigned char, 8> len{};
        for (int i = 0; i < 8; ++i) len[static_cast<std::size_t>(i)] = static_cast<unsigned char>(bits >> (8 * i));
        update(len.data(), len.size());
        std::string out;
        char buf[3];
        for (const std::uint32_t v : state_) {
            for (int i = 0; i < 4; ++i) {
                std::snprintf(buf, sizeof buf, "%02x", static_cast<unsigned>((v >> (8 * i)) & 0xffu));
                out += buf;
            }
        }
        return out;
    }

private:
    void block(const unsigned char* p) {
        std::array<std::uint32_t, 16> m{};
        for (std::size_t i = 0; i < 16; ++i) {
            m[i] = static_cast<std::uint32_t>(p[4 * i]) | static_cast<std::uint32_t>(p[4 * i + 1]) << 8 |
                   static_cast<std::uint32_t>(p[4 * i + 2]) << 16 | static_cast<std::uint32_t>(p[4 * i + 3]) << 24;
        }
        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        for (std::size_t i = 0; i < 64; ++i) {
            std::uint32_t f = 0;
            std::size_t g = 0;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            f = f + a + kMd5K[i] + m[g];
            a = d;
            d = c;
            c = b;
            b = b + rotl(f, kMd5S[(i / 16) * 4 + i % 4]);
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
    }

    std::array<std::uint32_t, 4> state_ = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    std::array<unsigned char, 64> buffer_{};
    std::size_t used_ = 0;
    std::uint64_t length_ = 0;
};

// --- File ---------------------------------------------------------------------------------------------------------

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw EngineError("esecuzione: non riesco a leggere " + p.string());
    std::ostringstream o;
    o << in.rdbuf();
    return o.str();
}

// Scrive in un nome temporaneo e rinomina: il file o c'e' tutto o non c'e'.
void write_atomic(const std::filesystem::path& p, const std::string& content) {
    std::filesystem::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw EngineError("esecuzione: non riesco a scrivere " + tmp.string());
        out << content;
        if (!out) throw EngineError("esecuzione: scrittura interrotta di " + tmp.string());
    }
    std::filesystem::rename(tmp, p);
}

std::string range_key(std::uint64_t first, std::uint64_t last) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%06llu-%06llu", static_cast<unsigned long long>(first),
                  static_cast<unsigned long long>(last));
    return buf;
}

}  // namespace

std::string md5_hex(std::string_view data) {
    Md5 m;
    m.update(reinterpret_cast<const unsigned char*>(data.data()), data.size());
    return m.hex();
}

std::string md5_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    Md5 m;
    std::array<char, 1 << 16> buf{};
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) m.update(reinterpret_cast<const unsigned char*>(buf.data()), static_cast<std::size_t>(got));
    }
    return m.hex();
}

void parallel_for(std::size_t n, int threads, const std::function<void(std::size_t)>& fn) {
    const std::size_t workers = std::min<std::size_t>(n, static_cast<std::size_t>(std::max(1, threads)));
    std::atomic<std::size_t> next{0};
    std::atomic<bool> failed{false};
    std::exception_ptr error;
    std::mutex error_mutex;
    auto loop = [&] {
        for (std::size_t i = next++; i < n && !failed; i = next++) {
            try {
                fn(i);
            } catch (...) {
                const std::lock_guard<std::mutex> lock(error_mutex);
                if (!error) error = std::current_exception();
                failed = true;
            }
        }
    };
    if (workers <= 1) {
        loop();
    } else {
        std::vector<std::thread> pool;
        pool.reserve(workers);
        for (std::size_t t = 0; t < workers; ++t) pool.emplace_back(loop);
        for (std::thread& t : pool) t.join();
    }
    if (error) std::rethrow_exception(error);
}

std::string block_file_name(std::string_view name, std::uint64_t first, std::uint64_t last) {
    return std::string(name) + "_" + range_key(first, last) + ".jsonl";
}

RunResult run_resumable(const std::filesystem::path& dir, const RunSpec& spec, const BlockWork& work,
                        const RunOptions& options) {
    using nlohmann::ordered_json;
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    if (spec.block == 0 || spec.last < spec.first || spec.files.empty()) {
        throw EngineError("esecuzione: intervallo, blocco o file non validi");
    }
    std::filesystem::create_directories(dir);
    const std::filesystem::path manifest_path = dir / "manifest.json";

    ordered_json identity;
    identity["config"] = ordered_json::parse(spec.config);
    identity["seed_da"] = spec.first;
    identity["seed_a"] = spec.last;
    identity["blocco"] = spec.block;
    identity["file"] = spec.files;

    ordered_json manifest;
    if (std::filesystem::exists(manifest_path)) {
        manifest = ordered_json::parse(read_file(manifest_path));
        for (const auto& [key, value] : identity.items()) {
            if (!manifest.contains(key) || manifest[key] != value) {
                throw EngineError("esecuzione: il manifest di " + dir.string() + " ha un'altra configurazione (" + key +
                                  "): non si mescolano blocchi di due esecuzioni");
            }
        }
    } else {
        manifest = identity;
        manifest["blocchi"] = ordered_json::object();
    }

    std::vector<std::pair<std::uint64_t, std::uint64_t>> blocks;
    for (std::uint64_t b = spec.first; b <= spec.last; b += spec.block) {
        blocks.emplace_back(b, std::min(spec.last, b + spec.block - 1));
        if (spec.last - b < spec.block) break;  // niente overflow con l'ultimo blocco
    }

    RunResult result;
    result.blocks = static_cast<int>(blocks.size());
    auto block_ok = [&](const std::string& key, std::uint64_t first, std::uint64_t last) {
        if (!manifest["blocchi"].contains(key)) return false;
        const ordered_json& md5 = manifest["blocchi"][key]["md5"];
        for (const std::string& name : spec.files) {
            const std::filesystem::path p = dir / block_file_name(name, first, last);
            if (!md5.contains(name) || md5_file(p) != md5[name].get<std::string>()) return false;
        }
        return true;
    };

    int missing = 0;
    for (const auto& [first, last] : blocks) missing += block_ok(range_key(first, last), first, last) ? 0 : 1;
    int index = 0;
    for (const auto& [first, last] : blocks) {
        ++index;
        const std::string key = range_key(first, last);
        if (block_ok(key, first, last)) {
            ++result.skipped;
            continue;
        }
        if (options.max_blocks >= 0 && result.done_now >= options.max_blocks) break;
        const auto t0 = Clock::now();
        const std::vector<std::string> contents = work(first, last);
        if (contents.size() != spec.files.size()) throw EngineError("esecuzione: il blocco non ha tutti i file");
        ordered_json entry;
        for (std::size_t f = 0; f < spec.files.size(); ++f) {
            write_atomic(dir / block_file_name(spec.files[f], first, last), contents[f]);
            entry["md5"][spec.files[f]] = md5_hex(contents[f]);
        }
        const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
        entry["secondi"] = secs;
        manifest["blocchi"][key] = entry;
        manifest["completa"] = false;
        write_atomic(manifest_path, manifest.dump(2) + "\n");
        ++result.done_now;
        if (options.progress != nullptr) {
            const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            const int left = missing - result.done_now;
            char buf[256];
            std::snprintf(buf, sizeof buf, "[blocco %d/%d] seed %llu-%llu in %.0f s; mancano %d blocchi, circa %.0f s\n",
                          index, result.blocks, static_cast<unsigned long long>(first),
                          static_cast<unsigned long long>(last), secs, left, elapsed / result.done_now * left);
            *options.progress << buf << std::flush;
        }
    }

    result.complete = result.skipped + result.done_now == result.blocks;
    if (result.complete) {
        manifest["md5"] = ordered_json::object();
        for (const std::string& name : spec.files) {
            std::string all;
            for (const auto& [first, last] : blocks) all += read_file(dir / block_file_name(name, first, last));
            write_atomic(dir / (name + ".jsonl"), all);
            result.md5.push_back(md5_hex(all));
            manifest["md5"][name] = result.md5.back();
        }
        manifest["completa"] = true;
        write_atomic(manifest_path, manifest.dump(2) + "\n");
    }
    result.seconds = std::chrono::duration<double>(Clock::now() - start).count();
    return result;
}

}  // namespace mtg
