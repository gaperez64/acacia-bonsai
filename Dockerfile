#FROM ubuntu:25.04
FROM ubuntu:24.04

# install all dependencies
RUN apt update
RUN apt install -y zsh meson git gcc python-is-python3 \
                    ninja-build gnupg wget python3-pip \
                    pkg-config libglib2.0-dev libffi-dev libboost-dev \
                    autoconf automake libtool bison flex valgrind gpg \
                    libboost-all-dev

# tell Python that this system is disposable and global installs are OK
RUN python3 -m pip config set global.break-system-packages true

# needed to build the Python interface
# TODO: these need to be set to the latest version, but that currently does not work...
RUN pip3 install wheel meson-python build

# spot installation
RUN mkdir -p /etc/apt/keyrings
RUN wget https://www.lrde.epita.fr/repo/debian.gpg
RUN gpg --no-default-keyring --keyring ./tmp-kr.gpg --import debian.gpg
RUN gpg --no-default-keyring --keyring ./tmp-kr.gpg --export --output /etc/apt/keyrings/lrde.gpg
RUN sh -c "echo 'deb [signed-by=/etc/apt/keyrings/lrde.gpg] http://www.lrde.epita.fr/repo/debian/ stable/' >> /etc/apt/sources.list"
RUN apt update

# AMD
RUN wget http://launchpadlibrarian.net/815947228/libltdl7_2.5.4-4build1_amd64.deb
RUN dpkg -i libltdl7_2.5.4-4build1_amd64.deb

# ARM
#RUN wget http://ftp.de.debian.org/debian/pool/main/libt/libtool/libltdl7_2.5.4-9_arm64.deb
#RUN dpkg -i libltdl7_2.5.4-9_arm64.deb
#RUN apt -f install -y

# TODO: this seems to not run on arm. Continue on Ubuntu
RUN apt install -y spot libspot-dev

# TODO: pygraph?

RUN mkdir /opt/acacia_bonsai
WORKDIR /opt/acacia_bonsai

# TODO: how to exclude files?
COPY . .

RUN meson setup build
RUN meson compile -C build acacia-bonsai

#RUN mkdir -p /opt/acacia_bonsai/build
#WORKDIR /opt/acacia_bonsai/build

