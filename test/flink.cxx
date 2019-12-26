//
// "$Id$"
//
// Flink program for the Fast Light Tool Kit (FLTK).
//
// Copyright 2019 by Matthias Melcher and others.
//
// This library is free software. Distribution and use rights are outlined in
// the file "COPYING" which should have been included with this file.  If this
// file is missing or damaged, see the license at:
//
//     http://www.fltk.org/COPYING.php
//
// Please report all bugs and problems on the following page:
//
//     http://www.fltk.org/str.php
//

/*
 "Flink" creates an AndroidStudio project tree that compiles many FLTK test
 programes to run on Android.

 CMake does support the C++ (native) part of Android out of the box. Flink works
 on a higher layer and creates all the files needed make Android application
 packeages, including the required additional CMake files.

  Using the native fltk libraries there is no need to write any Java code.
 */

// TODO: make sure that there are no formatting characters in any of the path names (%s,...)
// TODO: handle all possible errors when writing files (if ( && && && ) else ...)
// TODO: document that source code, find better function and variable names

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include <FL/Fl.H>
#include <FL/fl_ask.H>
#include <FL/filename.H>
#include <FL/Fl_File_Chooser.H>

#include "flink_ui.h"

/*
 Manage a lsit of strings.

 FLTK does use templates or the C++ std library within its core or tests
 for historic reasons. So here is a crude implementation of vector<string>.
 */
class StringList
{
public:
  // Create an ampty list of strings.
  StringList(): pN(0), pCapacity(0), pArray(0L) { }
  // Create a prefilled list of strings. Last entry must be NULL.
  StringList(const char *firstEntry, ...): pN(0), pCapacity(0), pArray(0L) {
    add(firstEntry);
    va_list ap;
    va_start(ap, firstEntry);
    for (;;) {
      char *nextEntry = va_arg(ap,char*);
      if (!nextEntry) break;
      add(nextEntry);
    }
    va_end(ap);
  }
  // Destroy the list and release all resources.
  ~StringList() {
    for (int i=0; i<pN; ++i) free(pArray[i]);
    if (pArray) ::free(pArray);
  }
  // Append a string to the list, string is duplicated.
  void add(const char *string) {
    pMakeRoom();
    pArray[pN++] = ::strdup(string);
  }
  // Retunr the number of entries in the list.
  int n() const { return pN; }
  // Return the entry at the given index.
  const char *operator[](int index) const { return pArray[index]; }
private:
  // make room for one more entry.
  void pMakeRoom() {
    if (pN==pCapacity) {
      pCapacity += 32;
      pArray = (char**)::realloc(pArray, pCapacity*sizeof(char*));
    }
  }
  int pN, pCapacity;
  char **pArray;
};


// Pointer to the application window.
Fl_Window *gMainindow = NULL;

// Copy of the directory that contains the FLTK project tree.
char gFLTKRootDir[FL_PATH_MAX] = "";

// Copy of the subdirectory that contains the Android project tree.
char gProjectDir[FL_PATH_MAX] = "";

// List of all libraries created, will be filled in the process.
StringList gLibraryList;

// List of all applications created, will be filled in the process.
StringList gApplicationList;


/*
 Output a formatted string into a file. This is made to have the same footprint
 as fputs() to make formatting easy and the source code more readable.
 */
int fputf(const char *fmt, FILE *f, ...)
{
  va_list ap;
  va_start(ap, f);
  int ret = vfprintf(f, fmt, ap);
  va_end(ap);
  return ret;
}

/*
 Convinience function to easily create a file at the given
 directory and filename.
 */
FILE *createFile(const char *dir, const char *name)
{
  char filename[FL_PATH_MAX];
  strcpy(filename, dir);
  strcat(filename, name);
  fl_make_path_for_file(filename);
  return fl_fopen(filename, "wb");
}

/*
 Convinience function to easily create a file at the given
 directory, filename pattern, and additional parameters.
 */
FILE *createFileFormatted(const char *dir, const char *fmt, ...)
{
  char name[FL_PATH_MAX];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(name, FL_PATH_MAX, fmt, ap);
  va_end(ap);
  return createFile(dir, name);
}

/*
 Convinience function to easily create and write a text file at the given
 directory and filename.
 */
void createFile(const char *dir, const char *name, const char *text)
{
  char filename[FL_PATH_MAX];
  strcpy(filename, dir);
  strcat(filename, name);
  fl_make_path_for_file(filename);
  FILE *f = fl_fopen(filename, "wb");
  fwrite(text, strlen(text), 1, f);
  fclose(f);
}

/*
 Convinience function to easily create and write a binary file at the given
 directory and filename.
 */
void createFile(const char *dir, const char *name, const unsigned char *data, size_t size)
{
  char filename[FL_PATH_MAX];
  strcpy(filename, dir);
  strcat(filename, name);
  fl_make_path_for_file(filename);
  FILE *f = fl_fopen(filename, "wb");
  fwrite(data, size, 1, f);
  fclose(f);
}

/*
 Fills a list with entries to CMake variable by reading a CMakeLists.txt file.
 */
void getEntriesFromCMakeFile(StringList &list, const char *path, const char *name, const char *key)
{
  char clone = 0;
  char line[1024];
  char startKey[1024];
  snprintf(startKey, sizeof(startKey), "set (%s", key);
  int startKeyN = strlen(startKey);
  char filename[FL_PATH_MAX];
  snprintf(filename, FL_PATH_MAX, "%s/%s", path, name);
  FILE *f = fopen(filename, "rb");
  for (;;) {
    if (feof(f) || clone==2) break;
    fgets(line, sizeof(line), f);
    char *s = line;
    for (;;++s) {
      if (*s>' ') break;
    }
    memmove(line, s, strlen(s)+1);
    if (clone) {
      if (*s==')') {
        clone = 2;
      } else {
        s = line + strlen(line) - 1;
        for (;s>line;--s) {
          if (*s>' ') { s[1] = 0; break; }
        }
        list.add(line);
      }
    } else {
      if (strncmp(line, startKey, startKeyN)==0)
        clone = 1;
    }
  }
  fclose(f);
}

/*
 Create the file <Android>/<lib>/build.gradle.
 This file describes the steps required to build a library under Gradle.
 */
void createLibBuildGradle(const char *libName)
{
  FILE *f = createFileFormatted(gProjectDir, "%s/build.gradle", libName);
  fputs("apply plugin: 'com.android.library'\n\n"
        "android {\n"
        "  compileSdkVersion 26\n"
        "  defaultConfig {\n"
        "    minSdkVersion 14\n"
        "    targetSdkVersion 26\n"
        "    externalNativeBuild {\n"
        "      cmake {\n"
        "        arguments '-DANDROID_STL=c++_shared'\n", f);
  fputf("        targets '%s'\n", f, libName);
  fputs("      }\n"
        "    }\n"
        "  }\n"
        "  buildTypes {\n"
        "    release {\n"
        "      minifyEnabled false\n"
        "    }\n"
        "  }\n"
        "  externalNativeBuild {\n"
        "    cmake {\n"
        "      path 'src/main/cpp/CMakeLists.txt'\n"
        "    }\n"
        "  }\n"
        "}\n", f);
  fclose(f);
}

/*
 Create the file <Android>/<lib>/src/main/AndroidManifest.xml.
 This file describes the library to Android.
 */
void createLibSrcMainAndroidManifestXml(const char *libName)
{
  FILE *f;
  f = createFileFormatted(gProjectDir, "%s/src/main/AndroidManifest.xml", libName);
  fputs("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n", f);
  fputf("          package=\"org.fltk.%s\">\n", f, libName);
  fputs("</manifest>\n", f);
  fclose(f);
}

/*
 Create the file <Android>/<lib>/src/main/cpp/CMakeLists.txt.
 This file is used by the crosscopiler implementation of CMake to generate the
 native build environment for C and C++ source code.
 */
void createLibSrcMainCppCMakeListsTxt(const char *libName, const StringList &srcList)
{
  FILE *f = createFileFormatted(gProjectDir, "%s/src/main/cpp/CMakeLists.txt", libName);
  fputs("cmake_minimum_required(VERSION 3.6)\n"
        "\n"
        "set(CMAKE_VERBOSE_MAKEFILE on)\n"
        "\n", f);
  fputf("set(FLTK_DIR \"%s\")\n", f, gFLTKRootDir);
  fputf("set(FLTK_IDE_DIR \"%s\")\n", f, gProjectDir);
  fputs("set(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -std=c++11\")\n"
        "\n"
        "function(list_transform_prepend var prefix)\n"
        "    set(temp \"\")\n"
        "    foreach(f ${${var}})\n"
        "        list(APPEND temp \"${prefix}${f}\")\n"
        "    endforeach()\n"
        "    set(${var} \"${temp}\" PARENT_SCOPE)\n"
        "endfunction()\n"
        "\n"
        "set (CPPFILES\n", f);
  for (int i=0; i<srcList.n(); i++)
    fputf("  %s\n", f, srcList[i]);
  fputs(")\n"
        "\n"
        "add_definitions(-DFL_LIBRARY)\n"
        "\n"
        "list_transform_prepend(CPPFILES \"${FLTK_DIR}/src/\")\n"
        "\n"
        "# now build app's shared lib\n", f);
  fputf("add_library( %s STATIC\n", f, libName);
  fputs("  ${CPPFILES}\n"
        ")\n"
        "\n", f);
  fputf("set_target_properties( %s\n", f, libName);
  fputs("    PROPERTIES\n"
        "    CLEAN_DIRECT_OUTPUT TRUE\n"
        "    COMPILE_DEFINITIONS \"FL_LIBRARY\"\n"
        ")\n"
        "\n"
        "target_include_directories(\n", f);
  fputf("    %s SYSTEM PRIVATE\n", f, libName);
        // The path below is a terrible hack. The Android NDK include a file
        // name <version> somewhere, but instead of using the clang file,
        // if finds the FLTK "VERSION" file first. This path links directly to <version>
        // Alternative (cl/usr/include/cang only): -iwithsysroot /usr/include/c++/v1/
  fputs("    ${CMAKE_SYSROOT}/usr/include/c++/v1/\n"
        "    ${FLTK_DIR}/\n"
        "    ${FLTK_DIR}/src/\n"
        "    ${FLTK_IDE_DIR}/\n"
        ")\n"
        "\n"
        "target_include_directories(\n", f);
  fputf("    %s PRIVATE\n", f, libName);
  fputs("    ${FLTK_DIR}/src/ )\n", f);
  fclose(f);
}

/*
 Create all directories and files needed to compile a native library form
 C and C++ source code.
 */
void createLibraryFolder(const char *libName, const StringList &srcList)
{
  gLibraryList.add(libName);
  createLibBuildGradle(libName);
  createLibSrcMainAndroidManifestXml(libName);
  createLibSrcMainCppCMakeListsTxt(libName, srcList);
}

/*
 Create the file <Android>/<app>/build.gradle.
 This file describes the steps required to build an application under Gradle.
 */
void createAppBuildGradle(const char *appName, const StringList &libList)
{
  int i;
  FILE *f = createFileFormatted(gProjectDir, "%s/build.gradle", appName);
  fputs("apply plugin: 'com.android.application'\n"
        "android {\n"
        "    compileSdkVersion 26\n"
        "    dependencies {\n", f);
  for (i=0; i<libList.n(); ++i)
    fputf("        implementation project(':%s')\n", f, libList[i]);
  fputs("    }\n"
        "    defaultConfig {\n", f);
  fputf("        applicationId 'org.fltk.%s'\n", f, appName);
  fputs("        minSdkVersion 14\n"
        "        targetSdkVersion 26\n"
        "        externalNativeBuild {\n"
        "            cmake {\n"
        "                arguments '-DANDROID_STL=c++_shared'\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    buildTypes {\n"
        "        release {\n"
        "            minifyEnabled false\n"
        "        }\n"
        "    }\n"
        "    externalNativeBuild {\n"
        "        cmake {\n"
        "            path 'src/main/cpp/CMakeLists.txt'\n"
        "        }\n"
        "    }\n"
        "}\n", f);
  fclose(f);
}

/*
 Create the file <Android>/<app>/src/main/AndroidManifest.xml.

 "Every application must have an AndroidManifest.xml file (with precisely that
 name) in its root directory. The manifest presents essential information about
 the application to the Android system, information the system must have before
 it can run any of the application's code." - Android Documentation
 */
void createAppSrcMainAndroidManifestXml(const char *appName)
{
  FILE *f = createFileFormatted(gProjectDir, "%s/src/main/AndroidManifest.xml", appName);
  fputs("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n", f);
  fputf("          package=\"org.fltk.%s\"\n", f, appName);
  fputs("          android:versionCode=\"1\"\n"
        "          android:versionName=\"1.0\">\n"
        "  <application\n"
        "      android:allowBackup=\"false\"\n"
        "      android:fullBackupContent=\"false\"\n"
        "      android:icon=\"@mipmap/ic_launcher\"\n"
        "      android:label=\"@string/app_name\"\n"
        "      android:hasCode=\"false\">\n"
        "    <activity android:name=\"android.app.NativeActivity\"\n"
        "              android:label=\"@string/app_name\">\n"
        "      <meta-data android:name=\"android.app.lib_name\"\n", f);
  fputf("                android:value=\"%s\" />\n", f, appName);
  fputs("      <intent-filter>\n"
        "        <action android:name=\"android.intent.action.MAIN\" />\n"
        "        <category android:name=\"android.intent.category.LAUNCHER\" />\n"
        "      </intent-filter>\n"
        "    </activity>\n"
        "  </application>\n"
        "</manifest>\n", f);
  fclose(f);
}

/*
 Create the file <Android>/<app>/src/main/cpp/CMakeLists.txt.
 This file is used by the crosscopiler implementation of CMake to generate the
 native build environment for C and C++ source code.
 */
void createAppSrcMainCppCMakeListsTxt(const char *appName, const StringList &srcList, const StringList &libList)
{
  int i;
  FILE *f = createFileFormatted(gProjectDir, "%s/src/main/cpp/CMakeLists.txt", appName);
  fputs("cmake_minimum_required(VERSION 3.4.1)\n", f);
  fputf("set(FLTK_DIR \"%s\")\n", f, gFLTKRootDir);
  fputf("set(FLTK_IDE_DIR \"%s\")\n", f, gProjectDir);
  fputs("set(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} -std=c++11\")\n"
        "add_library(\n", f);
  fputf("    %s SHARED\n", f, appName);
  for (i=0; i<srcList.n(); ++i)
    fputf("    \"${FLTK_DIR}/test/%s\"\n", f, srcList[i]);
  fputs(")\n"
        "target_include_directories(\n", f);
  fputf("    %s SYSTEM PRIVATE\n", f, appName);
  fputs("    ${CMAKE_SYSROOT}/usr/include/c++/v1/\n"
        "    ${FLTK_DIR}/\n"
        "    ${FLTK_IDE_DIR}/\n"
        ")\n"
        "target_link_libraries(\n", f);
  fputf("    %s\n", f, appName);
  for (i=0; i<libList.n(); ++i)
    fputf("    \"${FLTK_IDE_DIR}/%s/.cxx/cmake/${CMAKE_BUILD_TYPE}/${ANDROID_ABI}/lib%s.a\"\n", f, libList[i], libList[i]);
  fputs("    android\n"
        "    log\n"
        "    m\n"
        ")\n", f);
  fclose(f);
}

/*
 Create the file <Android>/<app>/src/main/res/values/strings.xml.
 This file provdes a number of texts and strings available to the Android
 environment and the application istself. Currently it only contains the
 name of the app and FLTK statistics
 */
void createAppSrcMainResValuesStringsXml(const char *appName)
{
  FILE *f = createFileFormatted(gProjectDir, "%s/src/main/res/values/strings.xml", appName);
  fputs("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<resources>\n", f);
  fputf("    <string name=\"app_name\">%s</string>\n", f, appName);
  fputf("    <string name=\"fltk_version\">%d.%d.%d</string>\n", f,
        FL_MAJOR_VERSION, FL_MINOR_VERSION, FL_PATCH_VERSION);
  fputs("</resources>\n", f);
  fclose(f);
}

/*
 Create files in <Android>/<app>/src/main/res/.
 Copy a number of default app icons for various screen resolutions.
 We may add fonts here: <appName>/src/main/assets/fonts/Roboto-Regular.ttf
 */
void createAppSrcMainResBinaries(const char *appName)
{
  FILE *f;

  f = createFileFormatted(gProjectDir, "%s/src/main/res/mipmap-mdpi/ic_launcher.png", appName);
  fwrite(mdpi_ic_launcher, sizeof(mdpi_ic_launcher), 1, f);
  fclose(f);

  f = createFileFormatted(gProjectDir, "%s/src/main/res/mipmap-hdpi/ic_launcher.png", appName);
  fwrite(hdpi_ic_launcher, sizeof(hdpi_ic_launcher), 1, f);
  fclose(f);

  f = createFileFormatted(gProjectDir, "%s/src/main/res/mipmap-xhdpi/ic_launcher.png", appName);
  fwrite(xhdpi_ic_launcher, sizeof(xhdpi_ic_launcher), 1, f);
  fclose(f);

  f = createFileFormatted(gProjectDir, "%s/src/main/res/mipmap-xxhdpi/ic_launcher.png", appName);
  fwrite(xxhdpi_ic_launcher, sizeof(xxhdpi_ic_launcher), 1, f);
  fclose(f);
}

/*
 Create all directories and files needed to compile a native application form
 C and C++ source code.
 */
void createApplicationFolder(const char *appName, const StringList &srcList, const StringList &libList)
{
  gApplicationList.add(appName);
  createAppBuildGradle(appName, libList);
  createAppSrcMainAndroidManifestXml(appName);
  createAppSrcMainCppCMakeListsTxt(appName, srcList, libList);
  createAppSrcMainResValuesStringsXml(appName);
  createAppSrcMainResBinaries(appName);
}

/*
 Create the file <Android>/FL/abi-version.h
 This file is included from within FLTK and possibly from apps. The actual
 definition of this macro is in FL/Enumerations.H though.
 */
void createProjectFlAbiVersion()
{
  createFile(gProjectDir, "FL/abi-version.h",
             "/* #undef FL_ABI_VERSION */\n");
}

/*
 Create the file <Android>/build.gradle
 This file is needed for the basic setup of the Android buildtool "Gradle".
 */
void createProjectBuildGradle()
{
  createFile(gProjectDir, "build.gradle",
             "buildscript {\n"
             "    repositories {\n"
             "        jcenter()\n"
             "        google()\n"
             "    }\n"
             "    dependencies {\n"
             "        classpath 'com.android.tools.build:gradle:3.5.3'\n"
             "    }\n"
             "}\n\n"
             "allprojects {\n"
             "    repositories {\n"
             "        jcenter()\n"
             "        google()\n"
             "    }\n"
             "}\n");
}

/*
 Create the file <Android>/settings.gradle
 This file contains a list of subdirectories, one for each app, and one for
 each library, that need to be included into this project.
 */
void createProjectSettingsGradle()
{
  int i;
  FILE *f = createFile(gProjectDir, "settings.gradle");
  for (i=0; i<gLibraryList.n(); ++i)
    fputf("include ':%s'\n", f, gLibraryList[i]);
  for (i=0; i<gApplicationList.n(); ++i)
    fputf("include ':%s'\n", f, gApplicationList[i]);
  fclose(f);
}

/*
 Create the file <Android>/config.h
 This file is included by the FLTK core library to help the preprocessor when
 including files and calling functions depending on the OS and build
 environment. This file is usually created by CMake at configuration time.
 For Android, this is predefined here.
 */
void createProjectConfigH()
{
  createFile(gProjectDir, "config.h",
             "#define FLTK_DATADIR \"/usr/local/share/fltk\"\n"
             "#define FLTK_DOCDIR \"/usr/local/share/doc/fltk\"\n"
             "#define BORDER_WIDTH 2\n"
             "#define HAVE_GL 0\n"
             "#define HAVE_GL_GLU_H 0\n"
             "/* #undef HAVE_GLXGETPROCADDRESSARB */\n"
             "#define USE_COLORMAP 1\n"
             "#define HAVE_XINERAMA 0\n"
             "#define USE_XFT 0\n"
             "#define USE_PANGO 0\n"
             "#define HAVE_XDBE 0\n"
             "#define USE_XDBE HAVE_XDBE\n"
             "#define HAVE_XFIXES 0\n"
             "#define HAVE_XCURSOR 0\n"
             "#define HAVE_XRENDER 0\n"
             "#define HAVE_X11_XREGION_H 0\n"
             "/* #undef __APPLE_QUARTZ__ */\n"
             "/* #undef USE_X11 */\n"
             "/* #undef USE_SDL */\n"
             "#define HAVE_OVERLAY 0\n"
             "#define HAVE_GL_OVERLAY HAVE_OVERLAY\n"
             "#define WORDS_BIGENDIAN 0\n"
             "#define U16 unsigned short\n"
             "#define U32 unsigned\n"
             "#define U64 unsigned long\n"
             "#define HAVE_DIRENT_H 1\n"
             "#define HAVE_SCANDIR 1\n"
             "#define HAVE_SCANDIR_POSIX 1\n"
             "#define HAVE_VSNPRINTF 1\n"
             "#define HAVE_SNPRINTF 1\n"
             "#define HAVE_STRINGS_H 1\n"
             "#define HAVE_STRCASECMP 1\n"
             "#define HAVE_STRLCAT 1\n"
             "#define HAVE_STRLCPY 1\n"
             "#define HAVE_LOCALE_H 1\n"
             "#define HAVE_LOCALECONV 1\n"
             "#define HAVE_SYS_SELECT_H 1\n"
             "/* #undef HAVE_SYS_STDTYPES_H */\n"
             "#define USE_POLL 0\n"
             "#define HAVE_LIBPNG 1\n"
             "#define HAVE_LIBZ 1\n"
             "#define HAVE_LIBJPEG 1\n"
             "/* #undef FLTK_USE_CAIRO */\n"
             "/* #undef FLTK_HAVE_CAIRO */\n"
             "#define HAVE_PNG_H 1\n"
             "/* #undef HAVE_LIBPNG_PNG_H */\n"
             "#define HAVE_PNG_GET_VALID 1\n"
             "#define HAVE_PNG_SET_TRNS_TO_ALPHA 1\n"
             "#define FLTK_USE_NANOSVG 1\n"
             "#define HAVE_PTHREAD 1\n"
             "#define HAVE_PTHREAD_H 1\n"
             "/* #undef HAVE_ALSA_ASOUNDLIB_H */\n"
             "#define HAVE_LONG_LONG 1\n"
             "#define FLTK_LLFMT \"%lld\"\n"
             "#define FLTK_LLCAST (long long)\n"
             "#define HAVE_DLFCN_H 1\n"
             "#define HAVE_DLSYM 1\n"
             "#define FL_NO_PRINT_SUPPORT 1\n"
             "/* #undef FL_CFG_NO_FILESYSTEM_SUPPORT */\n");
}

/*
 Create all files that are needed by AndroidStudio and Gradle, independently
 of the apps and libs created.
 */
void createProjectFiles()
{
  createProjectFlAbiVersion();
  createProjectBuildGradle();
  createProjectSettingsGradle();
  createProjectConfigH();
}

/*
 Read all the user definable parameters from the user interface and
 store them in a convinient location.

 TODO: verify the parameters, warn the user, and return 0, so we can abort the mission.
 */
int updateProjectParametrsFromUI()
{
  char cwd[FL_PATH_MAX];
  fl_getcwd(cwd, FL_PATH_MAX);
  strcpy(gFLTKRootDir, wSourceFolder->value());
  fl_chdir(gFLTKRootDir);
  fl_filename_absolute(gProjectDir, FL_PATH_MAX, wProjectFolder->value());
  int n = strlen(gProjectDir);
  if (gProjectDir[n]!='/') strcat(gProjectDir, "/");
  fl_chdir(cwd);

  char idFile[FL_PATH_MAX];
  snprintf(idFile, FL_PATH_MAX, "%s/%s", gProjectDir, "FLTK4Android.txt");
  if (fl_access(idFile, W_OK)==0) {
    wDeleteProject->activate();
  } else {
    wDeleteProject->deactivate();
  }

  return 1;
}

int flink_rmdir(const char *fullpath)
{
  int i, ret = 0;
  struct dirent **entry;
  int n = fl_filename_list(fullpath, &entry);
  if (n==-1)
    return -1;
  if (n>2) { // more than "." and ".."
    for (i=0; i<n; i++) {
      const char *fn = entry[i]->d_name;
      if (strcmp(fn, "./")==0) continue;
      if (strcmp(fn, "../")==0) continue;
      char newPath[FL_PATH_MAX];
      snprintf(newPath, sizeof(newPath), "%s%s", fullpath, fn);
      if (fl_filename_isdir(newPath)) {
        ret = flink_rmdir(newPath); // recursion
      } else {
        ret = fl_unlink(newPath);
      }
      if (ret==-1)
        break;
    }
  }
  for (i=0; i<n; ++i)
    free(entry[i]);
  free(entry);
  if (ret==0)
    fl_rmdir(fullpath);
  return ret;
}

/*
 FLTK callback:
 Delete the entire AndroidStudio project tree.
 */
void deleteProject()
{
  if (updateProjectParametrsFromUI()==0)
    return;
  char idFile[FL_PATH_MAX];
  snprintf(idFile, FL_PATH_MAX, "%s/%s", gProjectDir, "FLTK4Android.txt");
  if (fl_access(idFile, W_OK)!=0) {
    int ret = fl_choice("This directory was not created by Flink.\n"
                        "Do you want to delete the directory anyway?\n\n%s\n",
                        "Cancel", "Delete Directory", 0L,
                        gProjectDir);
    if (ret==0) return; // Cancel
  } else {
    int ret = fl_choice("Do you want to delete this directory?\n\n%s\n",
                        "Cancel", "Delete Directory", 0L,
                        gProjectDir);
    if (ret==0) return; // Cancel
  }
  if (flink_rmdir(gProjectDir)==-1) {
    fl_alert("Error deleting directory:\n\n%s", gProjectDir);
  }
  updateProjectParametrsFromUI();
}

/*
 Verify that the given source directory is actually an FLTK project root
 directory by searching for <fltk>/src/CMakeListst.txt which we will need
 in the process of creating the project.
 */
int verifyFLTKRootDir()
{
  char fullName[FL_PATH_MAX];
  snprintf(fullName, sizeof(fullName), "%s/src/CMakeLists.txt", gFLTKRootDir);
  int ret;
  if ((ret = fl_access(fullName, R_OK))==-1) {
    fl_alert("This seleted FLTK root directory does not seem to be\n"
             "the base of an FLTK project.\n\n"
             "%s:\n\"%s/src/CMakeLists.txt\"",
             strerror(errno), gFLTKRootDir);
    return 0;
  }
  return 1;
}

/*
 FLTK callback:
 Create the entire AndroidStudio project tree for all applications
 and libraries.
 */
void createProject()
{
  if (updateProjectParametrsFromUI()==0)
    return;
  if (verifyFLTKRootDir()==0)
    return;

  // This file identifies an AndroidStudio project directory that was created
  // by Flink. The file should probably contain some more details on how and
  // when it was created.
  createFile(gProjectDir, "FLTK4Android.txt",
             "Created by Flink\n");

  StringList fltkSrcs("drivers/Android/Fl_Android_Application.cxx",
                      "drivers/Android/Fl_Android_System_Driver.cxx",
                      "drivers/Android/Fl_Android_Screen_Driver.cxx",
                      "drivers/Android/Fl_Android_Screen_Keyboard.cxx",
                      "drivers/Android/Fl_Android_Window_Driver.cxx",
                      "drivers/Android/Fl_Android_Image_Surface_Driver.cxx",
                      "drivers/Android/Fl_Android_Graphics_Driver.cxx",
                      "drivers/Android/Fl_Android_Graphics_Clipping.cxx",
                      "drivers/Android/Fl_Android_Graphics_Font.cxx",
                      0L);
  getEntriesFromCMakeFile(fltkSrcs, gFLTKRootDir, "src/CMakeLists.txt", "CPPFILES");
  getEntriesFromCMakeFile(fltkSrcs, gFLTKRootDir, "src/CMakeLists.txt", "CFILES");
  createLibraryFolder("fltk", fltkSrcs);

  StringList fltkFormsSrcs;
  getEntriesFromCMakeFile(fltkFormsSrcs, gFLTKRootDir, "src/CMakeLists.txt", "FLCPPFILES");
  createLibraryFolder("fltk_forms", fltkFormsSrcs);


  // test applications that can run on Android
  // - entries marked TODO basically work, but need to be adapted to the mobile platform
  // - entries marked with FIXME require additional work on FLTK
  // - unmarked entries work well, no more work is required

  createApplicationFolder("adjuster", StringList("adjuster.cxx", 0L), StringList("fltk", 0L));
  createApplicationFolder("arc", StringList("arc.cxx", 0L), StringList("fltk", 0L));
  // FIXME: alpha drawing not implemented
  createApplicationFolder("animated", StringList("animated.cxx", 0L), StringList("fltk", 0L));
  // TODO: timeout dialog seems to not work?
  createApplicationFolder("ask", StringList("ask.cxx", 0L), StringList("fltk", 0L));
  createApplicationFolder("bitmap", StringList("bitmap.cxx", 0L), StringList("fltk", 0L));
  // FIXME: no audio library, screen size
  //createApplicationFolder("blocks", StringList("blocks.cxx", 0L), StringList("fltk", "fltk_audio", 0L));
  // TODO: window does not fit the default screen size
  createApplicationFolder("boxtype", StringList("boxtype.cxx", 0L), StringList("fltk", 0L));
  // FIXME: we need to be able to add the referenced resource file
  createApplicationFolder("browser", StringList("browser.cxx", 0L), StringList("fltk", 0L));
  createApplicationFolder("button", StringList("button.cxx", 0L), StringList("fltk", 0L));
  createApplicationFolder("buttons", StringList("buttons.cxx", 0L), StringList("fltk", 0L));
  // FIXME: must implement fltk_images
  // createApplicationFolder("checkers", StringList("checkers.cxx", 0L), StringList("fltk", "fltk_images", 0L));
  // FIXME: no interface to get actual time, both windows overlapping
  createApplicationFolder("clock", StringList("clock.cxx", 0L), StringList("fltk", 0L));
  // FIXME: we need to be able to add the referenced resource file
  createApplicationFolder("colbrowser", StringList("colbrowser.cxx", 0L), StringList("fltk_forms", "fltk", 0L));
  createApplicationFolder("color_chooser", StringList("color_chooser.cxx", 0L), StringList("fltk", 0L));
  //CREATE_EXAMPLE(cursor cursor.cxx fltk ANDROID_OK)
  createApplicationFolder("curve", StringList("curve.cxx", 0L), StringList("fltk", 0L));
  //CREATE_EXAMPLE(demo demo.cxx fltk)
  //CREATE_EXAMPLE(device device.cxx fltk)
  //CREATE_EXAMPLE(doublebuffer doublebuffer.cxx fltk ANDROID_OK)
  // FIXME: missing Fl_Native_Filechooser
  //createApplicationFolder("editor", StringList("editor.cxx", 0L), StringList("fltk", 0L));
  //CREATE_EXAMPLE(fast_slow fast_slow.fl fltk ANDROID_OK)
  //CREATE_EXAMPLE(file_chooser file_chooser.cxx "fltk;fltk_images")
  //CREATE_EXAMPLE(flink "flink.cxx;flink_ui.fl" "fltk;fltk_images")
  createApplicationFolder("fonts", StringList("fonts.cxx", 0L), StringList("fltk", 0L));
  createApplicationFolder("forms", StringList("forms.cxx", 0L), StringList("fltk_forms", "fltk", 0L));
  createApplicationFolder("hello", StringList("hello.cxx", 0L), StringList("fltk", 0L));
  //CREATE_EXAMPLE(help_dialog help_dialog.cxx "fltk;fltk_images")
  //CREATE_EXAMPLE(icon icon.cxx fltk)
  //CREATE_EXAMPLE(iconize iconize.cxx fltk)
  // TODO: transparency
  createApplicationFolder("image", StringList("image.cxx", 0L), StringList("fltk", 0L));
  //createApplicationFolder("inactive", StringList("inactive.fl", 0L), StringList("fltk", 0L));
  // TODO: Android keyboard may cover text field
  createApplicationFolder("input", StringList("input.cxx", 0L), StringList("fltk", 0L));
  //CREATE_EXAMPLE(input_choice input_choice.cxx fltk)
  //CREATE_EXAMPLE(keyboard "keyboard.cxx;keyboard_ui.fl" fltk)
  //CREATE_EXAMPLE(label label.cxx "fltk;fltk_forms")
  //CREATE_EXAMPLE(line_style line_style.cxx fltk)
  //CREATE_EXAMPLE(list_visuals list_visuals.cxx fltk)
  //CREATE_EXAMPLE(mandelbrot "mandelbrot_ui.fl;mandelbrot.cxx" fltk)
  //CREATE_EXAMPLE(menubar menubar.cxx fltk)
  //CREATE_EXAMPLE(message message.cxx fltk)
  //CREATE_EXAMPLE(minimum minimum.cxx fltk)
  //CREATE_EXAMPLE(native-filechooser native-filechooser.cxx "fltk;fltk_images")
  //CREATE_EXAMPLE(navigation navigation.cxx fltk)
  createApplicationFolder("output", StringList("output.cxx", 0L), StringList("fltk_forms", "fltk", 0L));
  //CREATE_EXAMPLE(overlay overlay.cxx fltk)
  //CREATE_EXAMPLE(pack pack.cxx fltk)
  //CREATE_EXAMPLE(pixmap pixmap.cxx fltk)
  //CREATE_EXAMPLE(pixmap_browser pixmap_browser.cxx "fltk;fltk_images")
  //CREATE_EXAMPLE(preferences preferences.fl fltk)
  //CREATE_EXAMPLE(offscreen offscreen.cxx fltk)
  //CREATE_EXAMPLE(radio radio.fl fltk)
  //CREATE_EXAMPLE(resize resize.fl fltk)
  //CREATE_EXAMPLE(resizebox resizebox.cxx fltk)
  //CREATE_EXAMPLE(rotated_text rotated_text.cxx fltk)
  // FIXME: popup window clipping is not ok
  createApplicationFolder("scroll", StringList("scroll.cxx", 0L), StringList("fltk", 0L));
  //CREATE_EXAMPLE(subwindow subwindow.cxx fltk)
  //CREATE_EXAMPLE(sudoku sudoku.cxx "fltk;fltk_images;${AUDIOLIBS}")
  //CREATE_EXAMPLE(symbols symbols.cxx fltk)
  //CREATE_EXAMPLE(tabs tabs.fl fltk)
  //CREATE_EXAMPLE(table table.cxx fltk)
  //CREATE_EXAMPLE(threads threads.cxx fltk)
  //CREATE_EXAMPLE(tile tile.cxx fltk)
  //CREATE_EXAMPLE(tiled_image tiled_image.cxx fltk)
  //CREATE_EXAMPLE(tree tree.fl fltk)
  //CREATE_EXAMPLE(twowin twowin.cxx fltk)
  //CREATE_EXAMPLE(utf8 utf8.cxx fltk)
  //CREATE_EXAMPLE(valuators valuators.fl fltk)
  //CREATE_EXAMPLE(unittests unittests.cxx fltk)
  //CREATE_EXAMPLE(windowfocus windowfocus.cxx fltk)

  createProjectFiles();

  fl_message("Project created at\n%s", gProjectDir);
  gMainindow->hide();
}

/*
 FLTK callback:
 Show the "About" window.
 */
void showAboutWindow()
{
  fl_message("%s",
             "Flink creates all files needed to compile FLTK for Android.\n\n"
             "Flink was written for FLTK 1.4 and tested with\n"
             "AndroidStudio 3.5 ."
             );
}

/*
 FLTK callback:
 User changed the source folder.
 */
void sourceFolderChanged()
{
  updateProjectParametrsFromUI();
}

/*
 FLTK callback:
 Pop up a filechooser to select the FLTK root folder.
 */
void selectSourceFolder()
{
  const char *dir = fl_dir_chooser("Select the FLTK root folder", wSourceFolder->value(), 0);
  if (dir) {
    wSourceFolder->value(dir);
    updateProjectParametrsFromUI();
  }
}

/*
 FLTK callback:
 User changed the project folder.
 */
void projectFolderChanged()
{
  updateProjectParametrsFromUI();
}

/*
 FLTK callback:
 Pop up a filechooser to select the AndroidStudio project folder.
 */
void selectProjectFolder()
{
  char oldDir[FL_PATH_MAX];
  fl_getcwd(oldDir, FL_PATH_MAX);
  fl_chdir(wSourceFolder->value());
  const char *dir = fl_dir_chooser("Select the AndroidStudio subfolder", wProjectFolder->value(), 1);
  fl_chdir(oldDir);
  if (dir) {
    wProjectFolder->value(dir);
    updateProjectParametrsFromUI();
  }
}

/*
 Write default values into the UI.
 */
void presetUI()
{
  char pathToFLTK[FL_PATH_MAX];
  strcpy(pathToFLTK, __FILE__);
  char *name = (char*)fl_filename_name(pathToFLTK);
  if (name && name>pathToFLTK) name[-1] = 0;
  name = (char*)fl_filename_name(pathToFLTK);
  if (name && name>pathToFLTK) name[-1] = 0;
  wSourceFolder->value(pathToFLTK);
  wProjectFolder->value("build/AndroidStudio");
  wDeleteProject->deactivate();
}

/*
 The main app entry point.
 TODO: we may want to add command line parameters at some point.
 */
int main(int argc, char **argv)
{
  fl_message_title_default("Flink");
  gMainindow = createMainWindow();

  presetUI();
  updateProjectParametrsFromUI();

  gMainindow->show(argc, argv);
  return Fl::run();
}

//
// End of "$Id$".
//
