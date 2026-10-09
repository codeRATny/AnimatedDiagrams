; Animated Diagrams setup: included by the CPack Inno Setup generator at the top of the generated
; script (CPACK_INNOSETUP_EXTRA_SCRIPTS, cmake/Packaging.cmake). [Code] is in installer.pas.

[InstallDelete]
; Updating: the files of the previous version go away before the new ones are copied, so a DLL or a
; Qt plugin dropped by the new version does not stay behind. Only in the folder of the previous
; installation (a custom folder chosen for a first installation is never cleaned). User data
; (settings, user plugins, sessions) is in %APPDATA% and is not touched.
Type: filesandordirs; Name: "{app}\bin"; Check: UpdatingThisFolder
Type: filesandordirs; Name: "{app}\plugins"; Check: UpdatingThisFolder
Type: filesandordirs; Name: "{app}\samples"; Check: UpdatingThisFolder
Type: filesandordirs; Name: "{app}\skills"; Check: UpdatingThisFolder
Type: filesandordirs; Name: "{app}\share"; Check: UpdatingThisFolder

[CustomMessages]
english.UpdateNote=Animated Diagrams %1 is installed in this folder and will be updated to %2. Settings, user plugins and documents are kept.
russian.UpdateNote=В этой папке установлена Animated Diagrams %1 — она будет обновлена до версии %2. Настройки, пользовательские плагины и документы сохранятся.
english.ReinstallNote=Animated Diagrams %1 is already installed and will be reinstalled.
russian.ReinstallNote=Animated Diagrams %1 уже установлена и будет переустановлена.
english.DowngradeQuestion=A newer version of Animated Diagrams (%1) is installed.%n%nReplace it with the older version %2?
russian.DowngradeQuestion=Установлена более новая версия Animated Diagrams (%1).%n%nЗаменить её более старой версией %2?
