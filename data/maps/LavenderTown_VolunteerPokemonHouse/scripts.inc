LavenderTown_VolunteerPokemonHouse_MapScripts::
	.byte 0

LavenderTown_VolunteerPokemonHouse_EventScript_MrFuji::
	lock
	faceplayer
	goto_if_set FLAG_GOT_POKE_FLUTE, LavenderTown_VolunteerPokemonHouse_EventScript_AlreadyHavePokeFlute
	msgbox LavenderTown_VolunteerPokemonHouse_Text_IdLikeYouToHaveThis
	checkitemspace ITEM_POKE_FLUTE
	goto_if_eq VAR_RESULT, FALSE, LavenderTown_VolunteerPokemonHouse_EventScript_NoRoomForPokeFlute
	setflag FLAG_GOT_POKE_FLUTE
	giveitem_msg LavenderTown_VolunteerPokemonHouse_Text_ReceivedPokeFluteFromMrFuji, ITEM_POKE_FLUTE, 1, MUS_OBTAIN_KEY_ITEM
	msgbox LavenderTown_VolunteerPokemonHouse_Text_ExplainPokeFlute
	call LavenderTown_VolunteerPokemonHouse_EventScript_TryGiveMegaStone
	release
	end

LavenderTown_VolunteerPokemonHouse_EventScript_AlreadyHavePokeFlute::
	msgbox LavenderTown_VolunteerPokemonHouse_Text_HasPokeFluteHelpedYou
	call LavenderTown_VolunteerPokemonHouse_EventScript_TryGiveMegaStone
	release
	end

@ Mega Evolution (3DS port): Mr. Fuji's MEGA STONE, given with the POKé FLUTE
@ (or the next time he is spoken to, on a save that already has the flute).
LavenderTown_VolunteerPokemonHouse_EventScript_TryGiveMegaStone::
	goto_if_set FLAG_GOT_MEGA_STONE_FROM_FUJI, LavenderTown_VolunteerPokemonHouse_EventScript_MegaStoneReturn
	msgbox LavenderTown_VolunteerPokemonHouse_Text_OneMoreThing
	checkitemspace ITEM_MEGA_STONE
	goto_if_eq VAR_RESULT, FALSE, LavenderTown_VolunteerPokemonHouse_EventScript_NoRoomForMegaStone
	giveitem ITEM_MEGA_STONE
	setflag FLAG_GOT_MEGA_STONE_FROM_FUJI
	msgbox LavenderTown_VolunteerPokemonHouse_Text_LetYourPartnerHoldIt
LavenderTown_VolunteerPokemonHouse_EventScript_MegaStoneReturn::
	return

LavenderTown_VolunteerPokemonHouse_EventScript_NoRoomForMegaStone::
	msgbox LavenderTown_VolunteerPokemonHouse_Text_MustMakeRoomForThis
	return

LavenderTown_VolunteerPokemonHouse_Text_OneMoreThing::
	.string "MR. FUJI: There is one more thing.\p"
	.string "This stone was found long ago with\n"
	.string "a POKéMON that loved its trainer.\p"
	.string "It is a MEGA STONE. I would like\n"
	.string "your partner to have it.$"

LavenderTown_VolunteerPokemonHouse_Text_LetYourPartnerHoldIt::
	.string "Let your partner hold it.\p"
	.string "They say that when a POKéMON and\n"
	.string "its trainer trust each other, the\l"
	.string "stone answers a KEY STONE…$"

LavenderTown_VolunteerPokemonHouse_EventScript_NoRoomForPokeFlute::
	msgbox LavenderTown_VolunteerPokemonHouse_Text_MustMakeRoomForThis
	release
	end

LavenderTown_VolunteerPokemonHouse_EventScript_LittleGirl::
	lock
	faceplayer
	famechecker FAMECHECKER_MRFUJI, 0
	msgbox LavenderTown_VolunteerPokemonHouse_Text_MrFujiLooksAfterOrphanedMons
	release
	end

LavenderTown_VolunteerPokemonHouse_EventScript_LittleBoy::
	msgbox LavenderTown_VolunteerPokemonHouse_Text_MonsNiceToHug, MSGBOX_NPC
	end

LavenderTown_VolunteerPokemonHouse_EventScript_Youngster::
	lock
	faceplayer
	goto_if_set FLAG_RESCUED_MR_FUJI, LavenderTown_VolunteerPokemonHouse_EventScript_YoungsterFujiBack
	msgbox LavenderTown_VolunteerPokemonHouse_Text_WhereDidMrFujiGo
	release
	end

LavenderTown_VolunteerPokemonHouse_EventScript_YoungsterFujiBack::
	msgbox LavenderTown_VolunteerPokemonHouse_Text_MrFujiWasPrayingForCubonesMother
	release
	end

LavenderTown_VolunteerPokemonHouse_EventScript_Nidorino::
	lock
	faceplayer
	waitse
	playmoncry SPECIES_NIDORINO, CRY_MODE_NORMAL
	msgbox LavenderTown_VolunteerPokemonHouse_Text_Nidorino
	waitmoncry
	release
	end

LavenderTown_VolunteerPokemonHouse_EventScript_Psyduck::
	lock
	faceplayer
	waitse
	playmoncry SPECIES_PSYDUCK, CRY_MODE_NORMAL
	msgbox LavenderTown_VolunteerPokemonHouse_Text_Psyduck
	waitmoncry
	release
	end

LavenderTown_VolunteerPokemonHouse_EventScript_PokemonFanMagazine::
	lockall
	famechecker FAMECHECKER_MRFUJI, 2
	msgbox LavenderTown_VolunteerPokemonHouse_Text_GrandPrizeDrawingClipped
	releaseall
	end

LavenderTown_VolunteerPokemonHouse_EventScript_Bookshelf::
	msgbox LavenderTown_VolunteerPokemonHouse_Text_PokemonMagazinesLineShelf, MSGBOX_SIGN
	end
