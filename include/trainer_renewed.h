#ifndef GUARD_TRAINER_RENEWED_H
#define GUARD_TRAINER_RENEWED_H

/* Renewed trainer teams (3DS port): see src/trainer_renewed.c. */
u8 Renewed_CreateParty(struct Pokemon *party, u16 trainerNum);
void Renewed_GiveMegaStone(struct Pokemon *party, u16 trainerNum, u8 count);

#endif // GUARD_TRAINER_RENEWED_H
