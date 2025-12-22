FROM debian:stable

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
RUN wget -q -O /etc/apt/keyrings/lre-epita.gpg https://www.lre.epita.fr/repo/debian.gpg
RUN echo "deb [signed-by=/etc/apt/keyrings/lre-epita.gpg] http://www.lre.epita.fr/repo/debian/ stable/" > /etc/apt/sources.list.d/lre-epita.list
RUN apt-get update
RUN apt-get install -y spot libspot-dev spot-doc python3-spot # Or a subset of those

# TODO: pygraph?

RUN mkdir /opt/acacia_bonsai
WORKDIR /opt/acacia_bonsai

# TODO: how to exclude files?
COPY . .

RUN meson setup build
RUN meson compile -C build acacia-bonsai
RUN meson install -C build --dry-run

#RUN mkdir -p /opt/acacia_bonsai/build
#WORKDIR /opt/acacia_bonsai/build

