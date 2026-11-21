# The bmcloud device channel protocol: encoding and decoding of the binary
# protobuf envelopes defined by the bmcloud-proto submodule. Transport is not
# its business — the callers hand over buffers and ship them however they like.
idf_component_register(
    SRCS "bmcloud-channel.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES bmcloud-pb nanopb)
