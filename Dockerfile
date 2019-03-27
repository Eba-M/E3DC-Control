FROM node:latest
#FROM arm32v6/alpine:3.7

# Surpress Upstart errors/warning
RUN dpkg-divert --local --rename --add /sbin/initctl
RUN ln -sf /bin/true /sbin/initctl

# Let the container know that there is no tty
ENV DEBIAN_FRONTEND noninteractive

# Install software requirements
RUN apt-get update && \
apt-get install -y git
RUN npm install -g node-gyp

#-- C++ Install
RUN apt-get install g++ make gcc

    
RUN echo "*** Installing gcc (4.9->8) and clang (3.8->6) ***" \
  && DEBIAN_FRONTEND=noninteractive apt-get update \
  && apt-get dist-upgrade -y \
  && echo "deb http://ftp.us.debian.org/debian jessie main contrib non-free" >> /etc/apt/sources.list.d/jessie.list \
  && echo "deb http://ftp.us.debian.org/debian unstable main contrib non-free" >> /etc/apt/sources.list.d/unstable.list \
  && apt-get update \
  && apt-get install -y cmake \
  && apt-get install -y  g++-7 g++-8 \
  && apt-get install -y clang++-6.0 \
  && apt-get autoremove --purge -y \
  && apt-get autoclean -y \
  && rm -rf /var/cache/apt/* /tmp/* \
  && echo "Setting g++ 8 as default compiler" \
  && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-8 1
  
RUN apt-get purge -y --auto-remove gcc libc6-dev make

# Clone the conf files into the docker contain
sudo mkdir /home/e3dc-control
chmod 777 /home/e3dc-control
WORKDIR /home/e3dc-control

RUN git clone https://github.com/Eba-M/E3DC-Control.git /home/e3dc-control
RUN /home/e3dc-control/E3DC_CONTROL
