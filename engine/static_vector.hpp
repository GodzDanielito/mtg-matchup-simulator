#pragma once

// Vettore a capacità fissa con i dati inline: niente allocazioni, e se T è
// trivially copyable lo è anche StaticVector (requisito per copiare GameState
// con un memcpy). Superare la capacità è un errore esplicito.

#include "types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>

namespace mtg {

template <typename T, std::size_t N>
class StaticVector {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(N <= 0xFFFF);

public:
    using value_type = T;
    using size_type = std::uint16_t;
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);

    [[nodiscard]] constexpr std::size_t size() const { return size_; }
    [[nodiscard]] constexpr bool empty() const { return size_ == 0; }
    [[nodiscard]] static constexpr std::size_t capacity() { return N; }

    constexpr T& operator[](std::size_t i) { return data_[i]; }
    constexpr const T& operator[](std::size_t i) const { return data_[i]; }
    constexpr T& back() { return data_[size_ - 1]; }
    constexpr const T& back() const { return data_[size_ - 1]; }

    constexpr T* begin() { return data_.data(); }
    constexpr T* end() { return data_.data() + size_; }
    constexpr const T* begin() const { return data_.data(); }
    constexpr const T* end() const { return data_.data() + size_; }

    void push_back(const T& value) {
        if (size_ >= N) throw EngineLimitError("StaticVector pieno (capacità " + std::to_string(N) + ")");
        data_[size_++] = value;
    }

    constexpr void pop_back() { --size_; }
    constexpr void clear() { size_ = 0; }

    // Inserisce in posizione `index` spostando avanti il resto (mantiene l'ordine).
    void insert(std::size_t index, const T& value) {
        if (size_ >= N) throw EngineLimitError("StaticVector pieno (capacità " + std::to_string(N) + ")");
        for (std::size_t i = size_; i > index; --i) data_[i] = data_[i - 1];
        data_[index] = value;
        ++size_;
    }

    // Rimuove l'elemento in posizione `index` mantenendo l'ordine degli altri.
    constexpr void erase_at(std::size_t index) {
        for (std::size_t i = index; i + 1 < size_; ++i) data_[i] = data_[i + 1];
        --size_;
    }

    [[nodiscard]] constexpr std::size_t find(const T& value) const {
        for (std::size_t i = 0; i < size_; ++i) {
            if (data_[i] == value) return i;
        }
        return npos;
    }

    [[nodiscard]] constexpr bool contains(const T& value) const { return find(value) != npos; }

private:
    std::array<T, N> data_{};
    size_type size_ = 0;
};

}  // namespace mtg
