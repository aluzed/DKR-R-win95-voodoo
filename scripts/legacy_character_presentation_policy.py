"""Checked, additive per-racer portrait/voice hooks; no generated C edits.

Only the two hash-pinned retail ELFs are admitted. The native lookup/call
instructions remain unchanged: callbacks redirect their local arguments.
"""
import copy
import hashlib
import struct

ELFS = {
    'us.v77': '15eb20705e4ccd8ad75c07f43f073d28674f0fe542bac721d32c0034395f4409',
    'us.v80': 'a3fd54e626af1e99a51751dafd0c6fa2498ea035722e1e4aed73df9cd94c7bbb',
}
# function, address, Settings+racer*24 register, lookup-address register, lw word
PORTRAITS = {
    'us.v77': [('postrace_load',0x80094ae8,10,13,0x8dae0000),
               ('postrace_load',0x80094b44,2,10,0x8d4b0000),
               ('results_render',0x80096b94,16,25,0x8f250000),
               ('func_80098774',0x80098984,3,25,0x8f2e0000),
               ('func_80098774',0x800989c0,15,14,0x8dcf0000)],
    'us.v80': [('postrace_load',0x80094fec,10,13,0x8dae0000),
               ('postrace_load',0x80095048,2,10,0x8d4b0000),
               ('results_render',0x800970bc,16,11,0x8d650000),
               ('func_80098774',0x80098ec0,3,25,0x8f2e0000),
               ('func_80098774',0x80098efc,15,14,0x8dcf0000)],
}

def compose_presentation(policy, elf, revision, sections, symbols):
    if revision not in ELFS or hashlib.sha256(elf.read_bytes()).hexdigest()!=ELFS[revision]:
        raise ValueError('Custom presentation requires a reviewed retail ELF')
    result=copy.deepcopy(policy)
    words={base+i:struct.unpack_from('>I',data,i)[0] for base,data in sections for i in range(0,len(data),4)}
    def one(name):
        values=symbols.get(name,set())
        if len(values)!=1:raise ValueError('Ambiguous presentation function '+name)
        return next(iter(values))
    def hook(name,pc,text,expected):
        start,size=one(name)
        if pc%4 or not start<=pc<start+size or words.get(pc)!=expected:
            raise ValueError('Presentation instruction changed: '+name)
        if any(int(p['vram'],0)==pc for p in result.get('instructionPatches',[])):
            raise ValueError('Presentation overlaps an instruction patch')
        owners=[h for h in result['functionHooks'] if int(h['beforeVram'],0)==pc]
        if owners:
            # The only shared entry is the already-validated selection-sound
            # adapter. Its handle-scoped path remains first and unchanged.
            if name!='sound_play' or len(owners)!=1 or owners[0]['function']!=name or \
               not owners[0]['text'].startswith('{ extern int dkr_legacy_character_menu(') or \
               not owners[0]['text'].endswith('8U, dkr_character_menu_fields)) return; }'):
                raise ValueError('Unreviewed presentation hook owner: '+name)
            owners[0]['text']+=' '+text
        else:
            result['functionHooks'].append({'function':name,'beforeVram':hex(pc),'text':text,
                'reason':'Session-owned custom portraits/voices for committed human racers only; preserve native layout, RNG, gating and audio lifetimes.'})
    for name,pc,subject,lookup,expected in PORTRAITS[revision]:
        hook(name,pc,'{ extern uint32_t dkr_legacy_character_portrait_lookup(uint8_t*, recomp_context*, uint32_t); '
             f'uint32_t cell = dkr_legacy_character_portrait_lookup(rdram, ctx, (uint32_t)ctx->r{subject}); '
             f'if (cell) ctx->r{lookup} = (int32_t)cell; }}',expected)
    voice,horn=(0x800571f0,0x8005708c) if revision=='us.v77' else (0x80057230,0x800570cc)
    for name,pc,target,racer,value,delay in [
        ('play_random_character_voice',voice,'audspat_play_sound_at_position',18,8,0x3104ffff),
        ('racer_play_sound',horn,'sound_play_spatial',2,4,0xafa00010)]:
        if words.get(pc+4)!=delay:raise ValueError('Race voice delay-slot ownership changed')
        hook(name,pc,'{ extern unsigned dkr_legacy_character_race_sound(uint8_t*, recomp_context*, uint32_t, unsigned); '
             f'ctx->r{value} = dkr_legacy_character_race_sound(rdram, ctx, (uint32_t)ctx->r{racer}, (unsigned)ctx->r{value}); }}',
             0x0c000000|((one(target)[0]>>2)&0x3ffffff))
    for kind,name in enumerate(('audspat_play_sound_at_position','sound_play_direct','sound_play')):
        pc=one(name)[0]
        hook(name,pc,'{ extern int dkr_legacy_character_play_sound(uint8_t*, recomp_context*, unsigned); '
             f'if (dkr_legacy_character_play_sound(rdram, ctx, {kind}U)) return; }}',words[pc])
    return result
