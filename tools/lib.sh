#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
#
# Adapted from the sibling backend xdg-desktop-portal-certificate's tools/lib.sh
# (same author, same licence).
#
# Shared fixture validation and portal configuration helpers; source this file.

# Fixture paths are validated before use and deletion; see docs/TESTING.md.

XDP_FIXTURE_MARKER=".xdg-desktop-portal-webauth-fixture"

lib_die() {
	echo "${0##*/}: $*" >&2
	exit 40
}

fixture_tmp_root() {
	local root="${TMPDIR:-/tmp}"

	root="$(realpath -m -- "$root")" || lib_die "could not resolve TMPDIR '$root'"
	printf '%s' "${root%/}"
}

fixture_no_symlinks() {
	local path="$1" lexical real

	lexical="$(realpath -m -s -- "$path")" || return 1
	real="$(realpath -e -- "$path" 2>/dev/null)" || return 1
	[ "$lexical" = "$real" ]
}

fixture_under_tmp() {
	local path real root

	path="$1"
	root="$(fixture_tmp_root)"
	real="$(realpath -m -- "$path")" || return 1

	case "$real" in
	"$root"/?*) return 0 ;;
	*) return 1 ;;
	esac
}

fixture_dir_safe() {
	local path="$1" owner mode sticky group other

	owner="$(stat -c %u -- "$path" 2>/dev/null)" || return 1
	[ "$owner" = "$(id -u)" ] || [ "$owner" = 0 ] || return 1

	mode="$(stat -c %04a -- "$path" 2>/dev/null)" || return 1
	sticky=$((0${mode:0:1} & 1))
	group=$((0${mode:2:1} & 2))
	other=$((0${mode:3:1} & 2))

	if [ "$group" -ne 0 ] || [ "$other" -ne 0 ]; then
		[ "$sticky" -eq 1 ] || return 1
	fi

	return 0
}

fixture_ancestors_ok() {
	local real="$1" root path

	root="$(fixture_tmp_root)"
	fixture_dir_safe "$root" ||
		lib_die "$root has unsafe ownership or permissions for fixtures"

	path="$(dirname -- "$real")"
	while [ "$path" != "$root" ] && [ "$path" != "/" ]; do
		fixture_dir_safe "$path" ||
			lib_die "$path has unsafe ownership or permissions on the fixture path"
		path="$(dirname -- "$path")"
	done
}

# The marker may only be written in a directory just created by this script.
fixture_claim() {
	local dir="$1" tag="$2"

	(umask 177 && printf '%s\n' "$tag" >"$dir/$XDP_FIXTURE_MARKER") ||
		lib_die "could not write the fixture marker in $dir"
	chmod 600 "$dir/$XDP_FIXTURE_MARKER" || lib_die "could not chmod the marker in $dir"
}

fixture_refuse() {
	lib_die "$2
Fixture use and removal refused. Remove it manually if disposable:
    rm -rf -- '$1'"
}

fixture_check() {
	local dir="$1" tag="$2" real marker owner mode claimed kind

	[ -L "$dir" ] && lib_die "$dir is a symlink; refusing to use it"
	[ -d "$dir" ] || lib_die "$dir exists and is not a directory"

	fixture_no_symlinks "$dir" ||
		lib_die "$dir is reached through a symlink; refusing to use it"

	fixture_under_tmp "$dir" ||
		lib_die "$dir is outside ${TMPDIR:-/tmp}; fixtures must be under TMPDIR"

	real="$(realpath -e -- "$dir")" || lib_die "could not resolve $dir"
	fixture_ancestors_ok "$real"

	owner="$(stat -c %u -- "$real")"
	[ "$owner" = "$(id -u)" ] ||
		fixture_refuse "$real" "$real must be owned by uid $(id -u); owner is $owner"

	marker="$real/$XDP_FIXTURE_MARKER"

	[ -L "$marker" ] &&
		fixture_refuse "$real" "$real has a symlink where its fixture marker should be."
	kind="$(stat -c %F -- "$marker" 2>/dev/null)" || kind=""
	[ "$kind" = "regular file" ] ||
		fixture_refuse "$real" "$real has no regular $XDP_FIXTURE_MARKER file"

	owner="$(stat -c %u -- "$marker")"
	[ "$owner" = "$(id -u)" ] ||
		fixture_refuse "$real" "Fixture marker in $real belongs to uid $owner"

	mode="$(stat -c %a -- "$marker")"
	[ "$mode" = "600" ] ||
		fixture_refuse "$real" "Fixture marker in $real must have mode 600; mode is $mode"

	claimed="$(cat -- "$marker" 2>/dev/null)"
	[ "$claimed" = "$tag" ] ||
		fixture_refuse "$real" "$real has fixture tag '$claimed'; expected '$tag'"

	# Unsafe fixture modes are refused without repair because prior writers may already have changed the contents.
	mode="$(stat -c %a -- "$real")"
	case "$mode" in
	700 | 500) ;;
	*)
		fixture_refuse "$real" "$real must have mode 700 or 500; mode is $mode"
		;;
	esac
}

# Exclusive creation prevents adoption of another process's directory.
fixture_create() {
	local dir="$1" tag="$2"

	fixture_under_tmp "$dir" ||
		lib_die "$dir is not under ${TMPDIR:-/tmp}; refusing to create a fixture there"

	(umask 077 && mkdir -- "$dir") || lib_die "could not create $dir"
	fixture_claim "$dir" "$tag"
}

fixture_make() {
	local dir="$1" tag="$2"

	[ -e "$dir" ] || [ -L "$dir" ] || fixture_create "$dir" "$tag"
	fixture_check "$dir" "$tag"
}

# Recursive deletion requires a validated real path and must not cross filesystem boundaries.
fixture_remove() {
	local dir="$1" tag="$2" real

	[ -e "$dir" ] || [ -L "$dir" ] || return 0
	fixture_check "$dir" "$tag"

	real="$(realpath -e -- "$dir")" || lib_die "could not resolve $dir"
	rm -rf --one-file-system -- "$real" || lib_die "could not remove $real"
}

fixture_mktemp() {
	local prefix="$1" tag="$2" dir

	dir="$(mktemp -d "${TMPDIR:-/tmp}/$prefix.XXXXXXXX")" || lib_die "mktemp failed"
	fixture_claim "$dir" "$tag"
	printf '%s' "$dir"
}

xdp_data_dirs() {
	local dirs

	printf '%s\n' "${XDG_DATA_HOME:-$HOME/.local/share}"
	dirs="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
	printf '%s\n' "$dirs" | tr ':' '\n'
}

# The frontend tries desktop-specific configuration in XDG_CURRENT_DESKTOP order before portals.conf.
xdp_desktops() {
	printf '%s\n' "${XDG_CURRENT_DESKTOP:-}" | tr ':' '\n' | tr '[:upper:]' '[:lower:]' |
		grep -v '^$'
}

# Load one matching configuration per directory.
xdp_config_chain() {
	local dir desktop found

	for dir in "${XDG_CONFIG_HOME:-$HOME/.config}/xdg-desktop-portal" \
		$(printf '%s\n' "${XDG_CONFIG_DIRS:-/etc/xdg}" | tr ':' '\n' | sed 's:$:/xdg-desktop-portal:') \
		/etc/xdg-desktop-portal \
		$(xdp_data_dirs | sed 's:$:/xdg-desktop-portal:') \
		/usr/share/xdg-desktop-portal; do
		found=""

		while read -r desktop; do
			if [ -f "$dir/$desktop-portals.conf" ]; then
				found="$dir/$desktop-portals.conf"
				break
			fi
		done < <(xdp_desktops)

		[ -z "$found" ] && [ -f "$dir/portals.conf" ] && found="$dir/portals.conf"
		[ -n "$found" ] && printf '%s\n' "$found"
	done | awk '!seen[$0]++'
}

# Resolve each interface across the configuration chain, taking interface candidates then defaults until a "none" entry.
xdp_flatten_preferred() {
	[ $# -gt 0 ] || return 0

	awk '
	function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
	function has_none(s,   n, a, i) {
		n = split(s, a, ";")
		for (i = 1; i <= n; i++) if (trim(a[i]) == "none") return 1
		return 0
	}
	function add(item) {
		if (item == "" || item == "none" || (item in used)) return
		used[item] = 1
		out = (out == "" ? item : out ";" item)
	}
	function addlist(s,   n, a, i) {
		n = split(s, a, ";")
		for (i = 1; i <= n; i++) add(trim(a[i]))
	}
	function terminate() { out = (out == "" ? "none" : out ";none"); return out }
	function flatten(key,   i, il, dl) {
		delete used
		out = ""
		for (i = 1; i <= nf; i++) {
			il = ((i SUBSEP key) in vals) ? vals[i, key] : NOKEY
			dl = ((i SUBSEP "default") in vals) ? vals[i, "default"] : NOKEY
			if (il != NOKEY) {
				if (has_none(il)) return terminate()
				addlist(il)
				if (dl != NOKEY) addlist(dl)
			} else if (dl != NOKEY) {
				if (has_none(dl)) return terminate()
				addlist(dl)
			}
		}
		return out
	}
	BEGIN { NOKEY = "\001none-such\001"; nf = 0; nk = 0 }
	FNR == 1 { nf++; section = "" }
	{
		line = trim($0)
		if (line ~ /^[#;]/ || line == "") next
		if (line ~ /^\[.*\]$/) { section = substr(line, 2, length(line) - 2); next }
		if (section != "preferred") next
		eq = index(line, "=")
		if (eq == 0) next
		key = trim(substr(line, 1, eq - 1))
		if (!(key in seen)) { seen[key] = 1; allkeys[++nk] = key }
		vals[nf, key] = trim(substr(line, eq + 1))
	}
	END {
		print "[preferred]"
		for (i = 1; i <= nk; i++) {
			if (allkeys[i] == "default") continue
			r = flatten(allkeys[i])
			if (r != "") printf "%s=%s;\n", allkeys[i], r
		}
		r = flatten("default")
		if (r != "") printf "default=%s;\n", r
	}
	' "$@"
}

# The generated file has one preferred section with one entry per key.
xdp_conf_set() {
	local file="$1" key="$2" value="$3" tmp="$1.tmp"

	awk -v k="$key" 'index($0, k "=") != 1' "$file" >"$tmp" || lib_die "could not rewrite $file"
	printf '%s=%s\n' "$key" "$value" >>"$tmp"
	mv -f -- "$tmp" "$file" || lib_die "could not replace $file"
}

# XDG_DESKTOP_PORTAL_DIR replaces all portal directories and configurations; preserve the machine's other portals.
xdp_write_portal_dir() {
	local dir="$1" repo="$2" mode="${3:-private}" source_dir base found
	local chain=()

	for source_dir in $(xdp_data_dirs | sed 's:$:/xdg-desktop-portal/portals:'); do
		[ -d "$source_dir" ] || continue

		for found in "$source_dir"/*.portal; do
			[ -f "$found" ] || continue
			base="$(basename "$found")"
			# The frontend uses the first portal file found for each name.
			[ -e "$dir/$base" ] && continue
			ln -s -- "$found" "$dir/$base"
		done
	done

	# The tested webauth.portal must override any installed copy.
	rm -f -- "$dir/webauth.portal"
	sed -e '/^#/d' -e '/^$/d' "$repo/data/webauth.portal.in" >"$dir/webauth.portal"

	mapfile -t chain < <(xdp_config_chain)

	# Plain portals.conf is read regardless of XDG_CURRENT_DESKTOP.
	if [ "${#chain[@]}" -gt 0 ]; then
		xdp_flatten_preferred "${chain[@]}" >"$dir/portals.conf"
		echo "${0##*/}: portals.conf flattened from ${#chain[@]} file(s)" >&2
		printf '%s: chain %s\n' "${0##*/}" "${chain[@]}" >&2
	else
		printf '[preferred]\ndefault=none;\n' >"$dir/portals.conf"
		echo "${0##*/}: no portals.conf on this machine; default=none" >&2
	fi

	xdp_conf_set "$dir/portals.conf" \
		org.freedesktop.impl.portal.experimental.WebAuthentication1 'webauth;'

	# A private bus has no keyring; disable Secret to avoid its 25-second activation timeout.
	if [ "$mode" != live ]; then
		xdp_conf_set "$dir/portals.conf" org.freedesktop.impl.portal.Secret 'none;'
	fi
}

xdp_experimental_portals() {
	local enabled="${XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL:-}" name

	for name in "$@"; do
		case ",$enabled," in
		*",$name,"*) ;;
		*) enabled="${enabled:+$enabled,}$name" ;;
		esac
	done
	printf '%s\n' "$enabled"
}

# Wait up to 30 seconds for the bus name, stopping if the expected process exits.
xdp_wait_for_name() {
	local name="$1" pid="$2" owned=""

	for _ in $(seq 1 120); do
		owned="$(gdbus call --session --dest org.freedesktop.DBus \
			--object-path /org/freedesktop/DBus \
			--method org.freedesktop.DBus.NameHasOwner "$name" 2>/dev/null)"
		case "$owned" in
		*true*) return 0 ;;
		esac

		kill -0 "$pid" 2>/dev/null || return 1
		sleep 0.25
	done

	return 1
}
