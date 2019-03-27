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
RUN apt-get install g++
RUN apk add --no-cache --virtual .build-deps make gcc g++ python && \
    npm install && \
    npm cache clean && \
    apk del .build-deps
    
RUN echo "*** Installing gcc (4.9->8) and clang (3.8->6) ***" \
  && DEBIAN_FRONTEND=noninteractive apt-get update \
  && apt-get dist-upgrade -y \
  && echo "deb http://ftp.us.debian.org/debian jessie main contrib non-free" >> /etc/apt/sources.list.d/jessie.list \
  && echo "deb http://ftp.us.debian.org/debian unstable main contrib non-free" >> /etc/apt/sources.list.d/unstable.list \
  && apt-get update \
  && apt-get install -y cmake \
  && apt-get install -y g++-4.9 g++-5 g++-6 g++-7 g++-8 \
  && apt-get install -y clang++-3.8 \
  && apt-get install -y clang++-3.9 \
  && apt-get install -y clang++-4.0 \
  && apt-get install -y clang++-5.0 \
  && apt-get install -y clang++-6.0 \
  && apt-get autoremove --purge -y \
  && apt-get autoclean -y \
  && rm -rf /var/cache/apt/* /tmp/* \
  && echo "Setting g++ 8 as default compiler" \
  && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-8 1
  
RUN apt-get purge -y --auto-remove gcc libc6-dev make

# Clone the conf files into the docker container
RUN git clone https://github.com/Eba-M/E3DC-Control.git /home/e3dc-control
RUN cd /home/e3dc-control
RUN make
RUN /home/e3dc-control/E3DC_CONTROL
#CMD ["/bin/bash", "cd /home/e3dc-control"]
#CMD ["/bin/bash", "/home/e3dc-control/make"]
#CMD ["/bin/bash", "/home/e3dc-control/E3DC_CONTROL"]
