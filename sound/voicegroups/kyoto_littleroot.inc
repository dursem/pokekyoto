@ PokeKyoto Littleroot original orchestra from Town.mp3. NO MIDI reconstruction.
@ Program 0, key C4.  Original GBA wave data loops in hardware's sound mixer.
@ Use voice_directsound (resampling enabled) so 13k/26k mixer choices stay in tune.
voice_group kyoto_littleroot
    voice_directsound 60, 0, DirectSoundWaveData_kyoto_littleroot, 255, 255, 255, 0
