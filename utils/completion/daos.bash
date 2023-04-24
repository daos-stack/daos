# shellcheck disable=SC1113
# /*
#  * (C) Copyright 2016-2023 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

_daos_control_comp()
{
	COMPREPLY=($(GO_FLAGS_COMPLETION=1 ${COMP_WORDS[0]} "${args[@]}"))
	local cur
	local words
	local cword
	_get_comp_words_by_ref -n : cur words cword
	args=("${words[@]:1:$cword}")

	local IFS=$'\n'

	COMPREPLY=($(GO_FLAGS_COMPLETION=1 ${COMP_WORDS[0]} "${args[@]}"))
	__ltrim_colon_completions "$cur"

	return 0
}

# these commands take advantage of automatic completion
complete -F _daos_control_comp dmg -o nospace
complete -F _daos_control_comp daos -o nospace
complete -F _daos_control_comp daos_agent
complete -F _daos_control_comp daos_server
