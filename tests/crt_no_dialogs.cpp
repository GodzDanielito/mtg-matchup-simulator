// L'eseguibile dei test non apre mai finestre di errore (regola in CLAUDE.md).
//
// In Debug il runtime di MSVC mostra asserzioni (anche quelle della libreria
// standard: indice fuori dai limiti, iteratore non valido) ed errori in una
// finestra modale, e Windows ne apre un'altra per i crash. Senza nessuno a
// chiuderle il processo resta fermo e sembra bloccato. Qui, prima di main:
//  - asserzioni, errori e avvisi del runtime Debug vanno su stderr; dopo il
//    messaggio un'asserzione termina il processo con abort();
//  - gli errori del runtime (chiamata virtuale pura e simili) vanno su stderr;
//  - abort() scrive il suo messaggio ma non chiama Windows Error Reporting;
//  - un crash (accesso non valido) non apre la finestra di Windows: il gestore
//    di Catch2 lo segnala e il processo termina.
// Controllato da tests/test_runtime.cpp ([runtime]).

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

namespace {

struct NoErrorDialogs {
    NoErrorDialogs() {
#if defined(_WIN32)
        SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
#if defined(_MSC_VER)
#if defined(_DEBUG)  // in Release i rapporti di Debug non esistono (le due funzioni sono macro vuote)
        for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
            _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
        }
#endif
        _set_error_mode(_OUT_TO_STDERR);
        _set_abort_behavior(_WRITE_ABORT_MSG, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    }
};

// Inizializzazione statica: avviene prima del main di Catch2, quindi prima di qualsiasi test.
const NoErrorDialogs g_no_error_dialogs;

}  // namespace
