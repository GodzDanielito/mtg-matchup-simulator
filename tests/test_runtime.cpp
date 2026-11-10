// L'eseguibile dei test non apre mai le finestre di errore di Debug di MSVC
// (tests/crt_no_dialogs.cpp, regola in CLAUDE.md): asserzioni ed errori della
// libreria standard vanno su stderr e un crash termina subito il processo.
// Senza, un'asserzione in Debug apre una finestra modale e il processo resta
// fermo in attesa di un clic, come se fosse bloccato.

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#if defined(_MSC_VER)
#include <crtdbg.h>
#include <stdlib.h>

#include <initializer_list>
#endif

TEST_CASE("Runtime: asserzioni ed errori su stderr, nessuna finestra di errore", "[runtime]") {
#if defined(_WIN32)
    // Crash (accesso non valido, eccezione non gestita): nessuna finestra di Windows.
    const UINT mode = GetErrorMode();
    CHECK((mode & SEM_NOGPFAULTERRORBOX) != 0);
    CHECK((mode & SEM_FAILCRITICALERRORS) != 0);
#endif
#if defined(_MSC_VER)
    // Errori del runtime (chiamata virtuale pura e simili): su stderr.
    CHECK(_set_error_mode(_REPORT_ERRMODE) == _OUT_TO_STDERR);
    // abort(): il messaggio si scrive, ma niente rapporto a Windows Error Reporting.
    const unsigned int abort_flags = _set_abort_behavior(0, 0);  // maschera vuota: legge senza cambiare
    CHECK((abort_flags & _CALL_REPORTFAULT) == 0);
    CHECK((abort_flags & _WRITE_ABORT_MSG) != 0);
#if defined(_DEBUG)
    // Asserzioni della libreria standard (_CRT_ASSERT), errori e avvisi del runtime Debug: su stderr.
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        INFO("tipo di rapporto " << type);
        CHECK(_CrtSetReportMode(type, _CRTDBG_REPORT_MODE) == _CRTDBG_MODE_FILE);
        // Il runtime memorizza il vero handle di stderr, non il valore simbolico _CRTDBG_FILE_STDERR.
        CHECK(_CrtSetReportFile(type, _CRTDBG_REPORT_FILE) == static_cast<_HFILE>(GetStdHandle(STD_ERROR_HANDLE)));
    }
#endif
#else
    SUCCEED("compilatore diverso da MSVC: nessuna finestra di errore da disattivare");
#endif
}
