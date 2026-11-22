EventScript_RepelWoreOff::
	special ShouldOfferAnotherRepel
	goto_if_eq VAR_RESULT, FALSE, EventScript_RepelWoreOffOnly
	lockall
	message Text_RepelWoreOff
	waitmessage
	waitbuttonpress
	bufferitemname STR_VAR_1, VAR_0x8004
	msgbox Text_UseAnotherRepel, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, NO, EventScript_RepelWoreOffDone
	removeitem VAR_0x8004, 1
	special SetRepelStepsFromLastRepel
	playse SE_REPEL
	bufferitemname STR_VAR_2, VAR_0x8004
	message gText_PlayerUsedVar2
	waitmessage
	waitse
EventScript_RepelWoreOffDone::
	closemessage
	releaseall
	end

EventScript_RepelWoreOffOnly::
	msgbox Text_RepelWoreOff, MSGBOX_SIGN
	end

Text_RepelWoreOff::
	.string "REPEL's effect wore off…$"

Text_UseAnotherRepel::
	.string "Use another {STR_VAR_1}?$"
