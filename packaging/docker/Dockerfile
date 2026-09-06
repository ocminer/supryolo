FROM nvidia/cuda:12.8.1-runtime-ubuntu22.04
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates libssl3 ocl-icd-libopencl1 \
    && rm -rf /var/lib/apt/lists/*
COPY supryolo /opt/supryolo/supryolo
COPY start.sh LICENSE THIRD_PARTY.md /opt/supryolo/
COPY third_party /opt/supryolo/third_party
ENV NVIDIA_VISIBLE_DEVICES=all NVIDIA_DRIVER_CAPABILITIES=compute,utility
WORKDIR /opt/supryolo
ENTRYPOINT ["./supryolo"]
CMD ["--help"]
