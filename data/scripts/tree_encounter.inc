@ Tree encounter: only an active tree's bottom metatiles return this script.
@ Consume BEFORE the battle, including when the player runs away or loses.
EventScript_TreeEncounter::
    lockall
    callnative TreeEncounter_CreateMon
    compare VAR_RESULT, 1
    goto_if_ne EventScript_TreeEncounter_End
    delay 30
    callnative TreeEncounter_EndEffect
    dowildbattle
EventScript_TreeEncounter_End:
    releaseall
    end

Debug_EventScript_TreeEncounter_Activate::
    lockall
    callnative TreeEncounter_DebugActivate
    compare VAR_RESULT, 1
    goto_if_eq Debug_TreeEncounter_Ready
    compare VAR_RESULT, 2
    goto_if_eq Debug_TreeEncounter_NoParty
    compare VAR_RESULT, 3
    goto_if_eq Debug_TreeEncounter_NoTree
    compare VAR_RESULT, 4
    goto_if_eq Debug_TreeEncounter_NoSprites
    compare VAR_RESULT, 5
    goto_if_eq Debug_TreeEncounter_NoStarter
    msgbox Debug_TreeEncounter_TextNoTable, MSGBOX_DEFAULT
    releaseall
    end
Debug_TreeEncounter_Ready:
    msgbox Debug_TreeEncounter_TextReady, MSGBOX_DEFAULT
    releaseall
    end
Debug_TreeEncounter_NoStarter:
    msgbox Debug_TreeEncounter_TextNoStarter, MSGBOX_DEFAULT
    releaseall
    end
Debug_TreeEncounter_NoParty:
    msgbox Debug_TreeEncounter_TextNoParty, MSGBOX_DEFAULT
    releaseall
    end
Debug_TreeEncounter_NoTree:
    msgbox Debug_TreeEncounter_TextNoTree, MSGBOX_DEFAULT
    releaseall
    end
Debug_TreeEncounter_NoSprites:
    msgbox Debug_TreeEncounter_TextNoSprites, MSGBOX_DEFAULT
    releaseall
    end

Debug_EventScript_TreeEncounter_Clear::
    lockall
    callnative TreeEncounter_DebugClear
    msgbox Debug_TreeEncounter_TextCleared, MSGBOX_DEFAULT
    releaseall
    end

Debug_TreeEncounter_TextReady:
    .string "Tree encounter activated.\n"
    .string "Look for falling green leaves.\p"
    .string "Face the bottom of the tree\n"
    .string "and press A.$"
Debug_TreeEncounter_TextNoTable:
    .string "No Tree encounter table here.\n"
    .string "Use Sendai, Route 101 or 103.$"
Debug_TreeEncounter_TextNoStarter:
    .string "Get your starter POKEMON first.\n"
    .string "Tree encounters are not ready yet.$"
Debug_TreeEncounter_TextNoParty:
    .string "A healthy POKEMON is needed.\n"
    .string "Stand on land to use this.$"
Debug_TreeEncounter_TextNoTree:
    .string "No reachable tree nearby.\n"
    .string "Try from a clear walking path.$"
Debug_TreeEncounter_TextNoSprites:
    .string "No space for the leaf effect.\n"
    .string "Close other effects and try again.$"
Debug_TreeEncounter_TextCleared:
    .string "Tree encounter cleared.$"
