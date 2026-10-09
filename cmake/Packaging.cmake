# CPack: DEB (Debian/Ubuntu), RPM (Fedora etc.), Inno Setup installer + ZIP (Windows).
# The generator is chosen in CI: cpack -G DEB | RPM | "INNOSETUP;ZIP" (CMake 3.27+, Inno Setup 6).

set(AD_PACKAGE_RELEASE_SUFFIX "" CACHE STRING "Package release suffix, e.g. ubuntu24.04 / debian13")

set(CPACK_PACKAGE_NAME "animated-diagrams")
set(CPACK_PACKAGE_VENDOR "Animated Diagrams")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Animated architecture diagram editor: request flows, timers, retries; GIF/WebM/MP4 export")
set(CPACK_PACKAGE_DESCRIPTION "Animated Diagrams draws architecture diagrams and animates request flows between services: messages, unavailability, timers/timeouts, retries, parallel actions and fallbacks. Animations export to GIF, PNG frames, WebM and MP4 (encoded with the FFmpeg libraries).")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_CONTACT "Animated Diagrams <noreply@animateddiagrams.app>")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Animated Diagrams")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_EXECUTABLES "animated-diagrams;Animated Diagrams")
set(CPACK_STRIP_FILES ON)

# ---- DEB --------------------------------------------------------------------
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_RELEASE "1${AD_PACKAGE_RELEASE_SUFFIX}")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)            # libqt6* dependencies via dpkg-shlibdeps
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins") # Qt platform plugins (xcb / wayland)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-translations-l10n") # standard Qt dialogs in the interface language

# ---- RPM --------------------------------------------------------------------
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)            # .fc43 etc.
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
set(CPACK_RPM_PACKAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)                 # libQt6*.so automatically
set(CPACK_RPM_PACKAGE_SUGGESTS "qt6-qttranslations") # standard Qt dialogs in the interface language
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _build_id_links none")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications
    /usr/share/icons
    /usr/share/icons/hicolor
    /usr/share/icons/hicolor/512x512
    /usr/share/icons/hicolor/512x512/apps
    /usr/share/icons/hicolor/scalable
    /usr/share/icons/hicolor/scalable/apps)

# ---- Windows: Inno Setup installer + portable ZIP ------------------------------
# A setup with the same AppId updates the installed copy in place: same folder and shortcuts, one entry
# in "Apps & features", the running application is closed first (Restart Manager), files of the previous
# version are replaced (packaging/windows/installer.iss), user data in %APPDATA% is kept. Older versions
# are not installed over newer ones without confirmation (packaging/windows/installer.pas).
set(CPACK_INNOSETUP_ARCHITECTURE x64)
set(CPACK_INNOSETUP_LANGUAGES english russian)
set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
set(CPACK_INNOSETUP_ICON_FILE "${PROJECT_SOURCE_DIR}/packaging/windows/animated-diagrams.ico")
set(CPACK_INNOSETUP_PROGRAM_MENU_FOLDER "Animated Diagrams")
set(CPACK_INNOSETUP_RUN_EXECUTABLES animated-diagrams)
set(CPACK_CREATE_DESKTOP_LINKS animated-diagrams)
set(CPACK_INNOSETUP_SETUP_AppId "AnimatedDiagrams") # never change: it identifies the installation
set(CPACK_INNOSETUP_SETUP_AppName "Animated Diagrams")
set(CPACK_INNOSETUP_SETUP_AppPublisher "${CPACK_PACKAGE_VENDOR}")
set(CPACK_INNOSETUP_SETUP_AppPublisherURL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_INNOSETUP_SETUP_AppSupportURL "${PROJECT_HOMEPAGE_URL}/issues")
set(CPACK_INNOSETUP_SETUP_AppUpdatesURL "${PROJECT_HOMEPAGE_URL}/releases")
set(CPACK_INNOSETUP_SETUP_UninstallDisplayName "Animated Diagrams")
set(CPACK_INNOSETUP_SETUP_UninstallDisplayIcon "{app}\\\\bin\\\\animated-diagrams.exe")
set(CPACK_INNOSETUP_SETUP_CloseApplications "force")
set(CPACK_INNOSETUP_SETUP_CloseApplicationsFilter "*.exe,*.dll")
set(CPACK_INNOSETUP_SETUP_RestartApplications OFF)
set(CPACK_INNOSETUP_SETUP_ChangesAssociations OFF)
set(CPACK_INNOSETUP_EXTRA_SCRIPTS "${PROJECT_SOURCE_DIR}/packaging/windows/installer.iss")
set(CPACK_INNOSETUP_CODE_FILES "${PROJECT_SOURCE_DIR}/packaging/windows/installer.pas")

include(CPack)
