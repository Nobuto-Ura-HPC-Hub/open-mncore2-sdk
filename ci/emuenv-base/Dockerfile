FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        libgomp1 xz-utils && rm -rf /var/lib/apt/lists/*
COPY mncore2_emuenv_20240826.tar.xz /tmp/
RUN cd /tmp && tar xJf mncore2_emuenv_20240826.tar.xz \
    && install -m 755 mncore2_emuenv_20240826/assemble3 /usr/local/bin/ \
    && install -m 755 mncore2_emuenv_20240826/gpfn3_package_main /usr/local/bin/ \
    && rm -rf /tmp/mncore2_emuenv_20240826*
