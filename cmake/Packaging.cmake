# CPack: DEB (Debian/Ubuntu), RPM (Fedora и др.), NSIS + ZIP (Windows).
# Генератор выбирается в CI: cpack -G DEB | RPM | "NSIS;ZIP".

set(AD_PACKAGE_RELEASE_SUFFIX "" CACHE STRING "Суффикс релиза пакета, напр. ubuntu24.04 / debian13")

set(CPACK_PACKAGE_NAME "animated-diagrams")
set(CPACK_PACKAGE_VENDOR "Animated Diagrams")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Animated architecture diagram editor: request flows, timers, retries; GIF/WebM/MP4 export")
set(CPACK_PACKAGE_DESCRIPTION "Animated Diagrams draws architecture diagrams and animates request flows between services: messages, unavailability, timers/timeouts, retries, parallel actions and fallbacks. Animations export to GIF, PNG frames, WebM and MP4 (via ffmpeg).")
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
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)            # зависимости от libqt6* — через dpkg-shlibdeps
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins") # платформенные плагины Qt (xcb/wayland)
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "ffmpeg")       # экспорт WebM/MP4

# ---- RPM --------------------------------------------------------------------
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)            # .fc43 и т.п.
set(CPACK_RPM_PACKAGE_LICENSE "MIT")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Productivity")
set(CPACK_RPM_PACKAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)                 # libQt6*.so — автоматически
set(CPACK_RPM_PACKAGE_SUGGESTS "ffmpeg")
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _build_id_links none")
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications
    /usr/share/icons
    /usr/share/icons/hicolor
    /usr/share/icons/hicolor/512x512
    /usr/share/icons/hicolor/512x512/apps
    /usr/share/icons/hicolor/scalable
    /usr/share/icons/hicolor/scalable/apps)

# ---- Windows ----------------------------------------------------------------
set(CPACK_NSIS_DISPLAY_NAME "Animated Diagrams")
set(CPACK_NSIS_PACKAGE_NAME "Animated Diagrams")
set(CPACK_NSIS_MUI_ICON "${PROJECT_SOURCE_DIR}/packaging/windows/animated-diagrams.ico")
set(CPACK_NSIS_MUI_UNIICON "${PROJECT_SOURCE_DIR}/packaging/windows/animated-diagrams.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\animated-diagrams.exe")
set(CPACK_NSIS_URL_INFO_ABOUT "${PROJECT_HOMEPAGE_URL}")
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$DESKTOP\\\\Animated Diagrams.lnk' '$INSTDIR\\\\bin\\\\animated-diagrams.exe'")
set(CPACK_NSIS_DELETE_ICONS_EXTRA "Delete '$DESKTOP\\\\Animated Diagrams.lnk'")

include(CPack)
