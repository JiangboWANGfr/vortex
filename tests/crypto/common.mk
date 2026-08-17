# tests/crypto/common.mk — forwards to the canonical
# tests/regression/common.mk so the crypto tests share the same
# build/run rules. Lets us reorganise tests/crypto/ later
# without copy-pasting common.mk per group.
include $(realpath $(dir $(lastword $(MAKEFILE_LIST))))/../regression/common.mk
