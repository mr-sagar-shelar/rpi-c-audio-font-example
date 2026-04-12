#!/bin/sh

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
. "$SCRIPT_DIR/lib/profile-common.sh"

apply_profile_dir "$SCRIPT_DIR/profiles/lcd-hat" "LCD HAT"
