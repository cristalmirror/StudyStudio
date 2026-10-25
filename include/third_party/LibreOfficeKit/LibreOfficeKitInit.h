/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_LIBREOFFICEKIT_LIBREOFFICEKITINIT_H
#define INCLUDED_LIBREOFFICEKIT_LIBREOFFICEKITINIT_H

#include <LibreOfficeKit/LibreOfficeKit.h>

#if defined __GNUC__ || defined __clang__
#  define LOK_TOLERATE_UNUSED __attribute__((used))
#else
#  define LOK_TOLERATE_UNUSED
#endif

#if defined(__linux__) || defined (__FreeBSD__) ||\
    defined(_WIN32) || defined(__APPLE__) || defined (__NetBSD__) ||\
    defined (__sun) || defined(__OpenBSD__) || defined(__EMSCRIPTEN__)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef _WIN32

    #include <dlfcn.h>

    #ifdef __APPLE__
        #define TARGET_LIB        "lib" "sofficeapp" ".dylib"
        #define TARGET_MERGED_LIB "lib" "mergedlo" ".dylib"
    #else
        #define TARGET_LIB        "lib" "sofficeapp" ".so"
        #define TARGET_MERGED_LIB "lib" "mergedlo" ".so"
    #endif
    #define SEPARATOR         '/'

#else

    #if !defined WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include  <windows.h>
    #include  <wchar.h>
    #define TARGET_LIB        "sofficeapp" ".dll"
    #define TARGET_MERGED_LIB "mergedlo" ".dll"
    #define SEPARATOR         '\\'
    #define UNOPATH           "\\..\\URE\\bin"

    #undef DELETE

#endif

#ifdef __cplusplus
extern "C"
{
#endif

#ifndef _WIN32

#if !defined(IOS)
        static void *lok_loadlib(const char *pFN)
    {
        return dlopen(pFN, RTLD_LAZY
#if defined LOK_LOADLIB_GLOBAL
                      | RTLD_GLOBAL
#endif
                      );
    }

    static char *lok_dlerror(void)
    {
        return dlerror();
    }

    // This function must be called to release memory allocated by lok_dlerror()
    static void lok_dlerror_free(char *pErrMessage)
    {
        (void)pErrMessage;
        // Do nothing for return of dlerror()
    }

    static void extendUnoPath(const char *pPath)
    {
        (void)pPath;
    }

    static void *lok_dlsym(void *Hnd, const char *pName)
    {
        return dlsym(Hnd, pName);
    }

    static int lok_dlclose(void *Hnd)
    {
        return dlclose(Hnd);
    }
#endif // IOS


#else
    static void *lok_loadlib(const char *pFN)
    {
        /* StudyStudio: paths from GTK are UTF-8; resolve dependencies beside
         * the LO DLL, including installations under Program Files. */
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pFN, -1, NULL, 0);
        if (!n) return NULL;
        wchar_t *wide = (wchar_t *) malloc((size_t)n * sizeof(wchar_t));
        if (!wide) return NULL;
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pFN, -1, wide, n))
        {
            free(wide);
            return NULL;
        }
        HMODULE handle = LoadLibraryExW(wide, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        DWORD error = GetLastError();
        free(wide);
        if (!handle) SetLastError(error);
        return (void *) handle;
    }

    static char *lok_dlerror(void)
    {
        LPSTR buf = NULL;
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, GetLastError(), 0, (LPSTR)&buf, 0, NULL); /* StudyStudio: C cast instead of reinterpret_cast */
        return buf;
    }

    // This function must be called to release memory allocated by lok_dlerror()
    static void lok_dlerror_free(char *pErrMessage)
    {
        HeapFree(GetProcessHeap(), 0, pErrMessage);
    }

    static void *lok_dlsym(void *Hnd, const char *pName)
    {
        return (void *) GetProcAddress((HINSTANCE) Hnd, pName); /* StudyStudio: C cast instead of reinterpret_cast */
    }

    static int lok_dlclose(void *Hnd)
    {
        return FreeLibrary((HINSTANCE) Hnd);
    }

    /* StudyStudio: preserve Unicode installation and existing PATH entries. */
    static void extendUnoPath(const char *pPath)
    {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pPath, -1, NULL, 0);
        if (!n) return;
        DWORD old_size = GetEnvironmentVariableW(L"PATH", NULL, 0);
        size_t capacity = 2 * (size_t)n + old_size + 32;
        wchar_t *value = (wchar_t *) malloc(capacity * sizeof(wchar_t));
        if (!value) return;
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pPath, -1, value, n))
        {
            free(value);
            return;
        }
        value[n - 1] = L';';
        memcpy(value + n, value, ((size_t)n - 1) * sizeof(wchar_t));
        wcscpy(value + 2 * n - 1, L"\\..\\URE\\bin;");
        size_t offset = wcslen(value);
        if (old_size && !GetEnvironmentVariableW(L"PATH", value + offset, old_size))
        {
            free(value);
            return;
        }
        SetEnvironmentVariableW(L"PATH", value);
        free(value);
    }

    static int lok_windows_stat(const char *path, WIN32_FILE_ATTRIBUTE_DATA *data)
    {
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, NULL, 0);
        if (!n) return -1;
        wchar_t *wide = (wchar_t *) malloc((size_t)n * sizeof(wchar_t));
        if (!wide) return -1;
        int ok = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, n)
            && GetFileAttributesExW(wide, GetFileExInfoStandard, data);
        free(wide);
        return ok ? 0 : -1;
    }

#endif

#if !defined(IOS)
static void *lok_dlopen( const char *install_path, char ** _imp_lib )
{
    char *imp_lib;
    void *dlhandle;

    size_t partial_length, imp_lib_size;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA dir_st;
#else
    struct stat dir_st;
#endif

    *_imp_lib = NULL;

    if (!install_path)
        return NULL;

#ifdef _WIN32
    if (lok_windows_stat(install_path, &dir_st) != 0)
#else
    if (stat(install_path, &dir_st) != 0)
#endif
    {
        fprintf(stderr, "installation path \"%s\" does not exist\n", install_path);
        return NULL;
    }

    // allocate large enough buffer
    partial_length = strlen(install_path);
    imp_lib_size = partial_length + sizeof(TARGET_LIB) + sizeof(TARGET_MERGED_LIB) + 2;
    imp_lib = (char *) malloc(imp_lib_size);
    if (!imp_lib)
    {
        fprintf( stderr, "failed to open library : not enough memory\n");
        return NULL;
    }

    memcpy(imp_lib, install_path, partial_length);

    extendUnoPath(install_path);

    imp_lib[partial_length++] = SEPARATOR;
    strncpy(imp_lib + partial_length, TARGET_LIB, imp_lib_size - partial_length);

    dlhandle = lok_loadlib(imp_lib);
    if (!dlhandle)
    {
        // If TARGET_LIB exists, and likely is a real library (not a
        // small one-line text stub as in the --enable-mergedlib
        // case), but dlopen failed for some reason, don't try
        // TARGET_MERGED_LIB.
#ifdef _WIN32
        WIN32_FILE_ATTRIBUTE_DATA st;
        if (lok_windows_stat(imp_lib, &st) == 0 &&
            (st.nFileSizeHigh != 0 || st.nFileSizeLow > 100))
#else
        struct stat st;
        if (stat(imp_lib, &st) == 0 && st.st_size > 100)
#endif
        {
            char *pErrMessage = lok_dlerror();
            fprintf(stderr, "failed to open library '%s': %s\n",
                    imp_lib, pErrMessage);
            lok_dlerror_free(pErrMessage);
            free(imp_lib);
            return NULL;
        }

        strncpy(imp_lib + partial_length, TARGET_MERGED_LIB, imp_lib_size - partial_length);

        dlhandle = lok_loadlib(imp_lib);
        if (!dlhandle)
        {
            char *pErrMessage = lok_dlerror();
            fprintf(stderr, "failed to open library '%s': %s\n",
                    imp_lib, pErrMessage);
            lok_dlerror_free(pErrMessage);
            free(imp_lib);
            return NULL;
        }
    }
    *_imp_lib = imp_lib;
    return dlhandle;
}
#endif

typedef LibreOfficeKit *(LokHookFunction)( const char *install_path);

typedef LibreOfficeKit *(LokHookFunction2)( const char *install_path, const char *user_profile_url );

typedef int             (LokHookPreInit)  ( const char *install_path, const char *user_profile_url );

typedef int             (LokHookPreInit2) ( const char *install_path, const char *user_profile_url, LibreOfficeKit** kit);

#if defined(IOS) || defined(ANDROID) || defined(__EMSCRIPTEN__)
LibreOfficeKit *libreofficekit_hook_2(const char* install_path, const char* user_profile_path);
#endif

static LibreOfficeKit *lok_init_2( const char *install_path,  const char *user_profile_url )
{
#if !defined(IOS) && !defined(ANDROID) && !defined(__EMSCRIPTEN__)
    void *dlhandle;
    char *imp_lib;
    LokHookFunction *pSym;
    LokHookFunction2 *pSym2;

    dlhandle = lok_dlopen(install_path, &imp_lib);
    if (!dlhandle)
        return NULL;

    pSym2 = (LokHookFunction2 *) lok_dlsym(dlhandle, "libreofficekit_hook_2");
    if (!pSym2)
    {
        if (user_profile_url != NULL)
        {
            fprintf( stderr, "the LibreOffice version in '%s' does not support passing a user profile to the hook function\n",
                     imp_lib );
            lok_dlclose( dlhandle );
            free( imp_lib );
            return NULL;
        }
        pSym = (LokHookFunction *) lok_dlsym( dlhandle, "libreofficekit_hook" );
        if (!pSym)
        {
            fprintf( stderr, "failed to find hook in library '%s'\n", imp_lib );
            lok_dlclose( dlhandle );
            free( imp_lib );
            return NULL;
        }
        free( imp_lib );
        // dlhandle is "leaked"
        // coverity[leaked_storage] - on purpose
        return pSym( install_path );
    }

    if (user_profile_url != NULL && user_profile_url[0] == '/')
    {
        // It should be either a file: URL or a vnd.sun.star.pathname: URL.
        fprintf( stderr, "second parameter to lok_init_2 '%s' should be a URL, not a pathname\n", user_profile_url );
        lok_dlclose( dlhandle );
        free( imp_lib );
        return NULL;
    }

    free( imp_lib );
    // dlhandle is "leaked"
    // coverity[leaked_storage] - on purpose
    return pSym2( install_path, user_profile_url );
#else
    return libreofficekit_hook_2( install_path, user_profile_url );
#endif
}

static LOK_TOLERATE_UNUSED
LibreOfficeKit *lok_init( const char *install_path )
{
    return lok_init_2( install_path, NULL );
}

#if !defined(IOS)
static LOK_TOLERATE_UNUSED
int lok_preinit( const char *install_path,  const char *user_profile_url )
{
    void *dlhandle;
    char *imp_lib;
    LokHookPreInit *pSym;

    dlhandle = lok_dlopen(install_path, &imp_lib);
    if (!dlhandle)
        return -1;

    pSym = (LokHookPreInit *) lok_dlsym(dlhandle, "lok_preinit");
    if (!pSym)
    {
        fprintf( stderr, "failed to find pre-init hook in library '%s'\n", imp_lib );
        lok_dlclose( dlhandle );
        free( imp_lib );
        return -1;
    }

    free( imp_lib );

    // dlhandle is "leaked"
    // coverity[leaked_storage] - on purpose
    return pSym( install_path, user_profile_url );
}
#endif

#undef SEPARATOR // It is used at least in enum class MenuItemType

#ifdef __cplusplus
}
#endif

#endif // defined(__linux__) || defined (__FreeBSD__) || defined(_WIN32) || defined(__APPLE__)

#endif // INCLUDED_LIBREOFFICEKIT_LIBREOFFICEKITINIT_H

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
