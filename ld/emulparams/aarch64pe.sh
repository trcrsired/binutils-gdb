ARCH="aarch64"
SCRIPT_NAME=pep
OUTPUT_FORMAT="pei-aarch64-little"
RELOCATEABLE_OUTPUT_FORMAT="pe-aarch64-little"
TEMPLATE_NAME=pep
SUBSYSTEM=PE_DEF_SUBSYSTEM
INITIAL_SYMBOL_CHAR=\"_\"
TARGET_PAGE_SIZE=0x1000
# Windows on ARM64 is increasingly run on systems with 16K pages
# (Apple Silicon, Android).  Default the section alignment to 64K so
# images work everywhere, including under Wine on those hosts.
OVERRIDE_SECTION_ALIGNMENT=0x10000
GENERATE_AUTO_IMPORT_SCRIPT=1
