FROM ubuntu:19.10

RUN apt-get -yq update \
 && apt-get -yq --no-install-suggests --no-install-recommends install \
    wget \
    git \
    avrdude \
    python-serial \
    xz-utils \
    ca-certificates \
    make \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/*

RUN wget https://downloads.arduino.cc/arduino-1.8.10-linux64.tar.xz \
 && tar xf *.tar.xz -C /opt \
 && rm *.tar.xz

RUN git clone https://github.com/sudar/Arduino-Makefile -b 1.5.2 /opt/arduino-mk

RUN git clone https://github.com/SpenceKonde/ATTinyCore.git -b v1.1.4 /opt/ATTinyCore

RUN git clone https://github.com/orangkucing/WireS.git /opt/WireS \
  && cd /opt/WireS \
  && git reset --hard 776ffa23b6b9b8ae17b21e10357e22b09e7e9510

RUN git clone https://github.com/orangkucing/analogComp /opt/analogComp

WORKDIR /src

CMD ["make"]
