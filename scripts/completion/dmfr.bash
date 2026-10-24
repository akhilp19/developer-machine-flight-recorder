#!/usr/bin/env bash
# Bash completion for dmfr

_dmfr() {
    local cur prev words cword
    _init_completion || return

    local commands="record diagnose start stop status query timeline events show snapshot diff bundle import-snapshot export retention config init hook install-wrappers"
    local global_opts="-c --config -h --help"

    local query_opts="--from --to --source --type --project --limit"
    local timeline_opts="--from --to --source --type --project --limit --group-by"
    local events_opts="--from --to --source --type --project --limit --offset"
    local show_opts="--window --context-limit --same-project --same-session"
    local snapshot_opts="--project --list --limit"
    local diff_opts="--snapshot --from-id --to-id --file --from --to --project --project-path"
    local bundle_opts="--from --to --project --project-path --scrub --limit"
    local import_snapshot_opts=""
    local export_opts="--from --to --source --type --project --limit --format"
    local retention_opts="--max-events --max-age-days"
    local config_subcommands="init show"
    local hook_opts="--repo --type --sha --branch --message --json --file"
    local init_opts=""

    if [[ ${cword} -eq 1 ]]; then
        COMPREPLY=( $(compgen -W "${commands} ${global_opts}" -- "${cur}") )
        return 0
    fi

    local cmd=${words[1]}

    case "${cmd}" in
        record)
            COMPREPLY=( $(compgen -W "--tool --" -- "${cur}") )
            ;;
        diagnose)
            COMPREPLY=( $(compgen -W "--event --project" -- "${cur}") )
            ;;
        query)
            COMPREPLY=( $(compgen -W "${query_opts}" -- "${cur}") )
            ;;
        timeline)
            COMPREPLY=( $(compgen -W "${timeline_opts}" -- "${cur}") )
            ;;
        events)
            COMPREPLY=( $(compgen -W "${events_opts}" -- "${cur}") )
            ;;
        show)
            COMPREPLY=( $(compgen -W "${show_opts}" -- "${cur}") )
            ;;
        snapshot)
            COMPREPLY=( $(compgen -W "${snapshot_opts}" -- "${cur}") )
            ;;
        diff)
            COMPREPLY=( $(compgen -W "${diff_opts}" -- "${cur}") )
            ;;
        bundle)
            COMPREPLY=( $(compgen -W "${bundle_opts}" -- "${cur}") )
            ;;
        import-snapshot)
            COMPREPLY=( $(compgen -W "${import_snapshot_opts}" -- "${cur}") )
            ;;
        export)
            COMPREPLY=( $(compgen -W "${export_opts}" -- "${cur}") )
            ;;
        retention)
            COMPREPLY=( $(compgen -W "${retention_opts}" -- "${cur}") )
            ;;
        config)
            if [[ ${cword} -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "${config_subcommands}" -- "${cur}") )
            fi
            ;;
        hook)
            if [[ ${cword} -eq 2 ]]; then
                COMPREPLY=( $(compgen -W "git build" -- "${cur}") )
            else
                COMPREPLY=( $(compgen -W "${hook_opts}" -- "${cur}") )
            fi
            ;;
        init)
            COMPREPLY=( $(compgen -W "${init_opts}" -- "${cur}") )
            ;;
        *)
            COMPREPLY=( $(compgen -W "${global_opts}" -- "${cur}") )
            ;;
    esac
}

complete -F _dmfr dmfr
