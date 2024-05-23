FROM gcc:11 as build

COPY docker/deps.sh deps.sh
COPY docker/config.sh config.sh
COPY docker/build.sh build.sh

RUN ./deps.sh
RUN ./config.sh
RUN ./build.sh

FROM debian:bullseye-slim as rippled
COPY --from=build /opt/ripple/bin /opt/ripple/bin
COPY --from=build /opt/ripple/etc /opt/ripple/etc

# Why isn't this linked?
RUN if [ $(uname -m) = "aarch64" ];then apt-get update && apt-get install libatomic1 && apt-get clean && rm -rf /var/cache/apt/lists; fi

CMD /opt/ripple/bin/rippled --version
