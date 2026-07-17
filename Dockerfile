# Образ со всем необходимым для сборки установщиков Electron:
# node + wine (для .exe/NSIS) + инструменты Linux-пакетов (deb) + rpm.
# База electronuserland/builder:wine уже содержит Node, wine, dpkg/fakeroot.
FROM electronuserland/builder:wine

# rpm-сборка требует rpmbuild
RUN apt-get update \
 && apt-get install -y --no-install-recommends rpm \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /project
