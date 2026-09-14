"""Synthetic Patch Pipeline guards; no private ROMs, generated C or submodules edited."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("legacy_compose", ROOT / "scripts/compose_legacy_mod_policy.py")
compose = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compose)
spec = importlib.util.spec_from_file_location("legacy_presentation", ROOT / "scripts/legacy_character_presentation_policy.py")
presentation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(presentation)


class PipelineTests(unittest.TestCase):
    def test_presentation_rejects_conflicts_and_changed_signatures(self):
        class FixtureElf:
            def read_bytes(self): return b'isolated synthetic presentation fixture'
        elf = FixtureElf()
        for rev in ('us.v77', 'us.v80'):
            words = {}; bounds = {}
            def put(name, pc, word):
                words[pc] = word
                lo, hi = bounds.get(name, (pc, pc))
                bounds[name] = min(lo, pc), max(hi, pc)
            for name, pc, subject, lookup, expected in presentation.PORTRAITS[rev]:
                put(name, pc, expected)
            voice, horn = (0x800571f0, 0x8005708c) if rev == 'us.v77' else (0x80057230, 0x800570cc)
            entries = ('audspat_play_sound_at_position', 'sound_play_direct', 'sound_play', 'sound_play_spatial')
            for i, name in enumerate(entries): put(name, 0x80001000 + i * 0x100, 0x27bdffe0)
            put('play_random_character_voice', voice, 0x0c000400)
            put('play_random_character_voice', voice+4, 0x3104ffff)
            put('racer_play_sound', horn, 0x0c0004c0)
            put('racer_play_sound', horn+4, 0xafa00010)
            sections = [(pc, struct.pack('>I', word)) for pc, word in words.items()]
            symbols = {name: {(lo, hi-lo+4)} for name, (lo, hi) in bounds.items()}
            base = {'instructionPatches': [], 'functionHooks': []}
            with self.assertRaises(ValueError): presentation.compose_presentation(base, elf, rev, sections, symbols)
            with patch.dict(presentation.ELFS, {rev: hashlib.sha256(elf.read_bytes()).hexdigest()}):
                result = presentation.compose_presentation(base, elf, rev, sections, symbols)
                self.assertEqual(base, {'instructionPatches': [], 'functionHooks': []})
                self.assertEqual(len(result['functionHooks']), 10)
                for site in result['functionHooks']:
                    for key, address in (('functionHooks', 'beforeVram'), ('instructionPatches', 'vram')):
                        bad = copy.deepcopy(base); bad[key].append({address: site['beforeVram'], 'function': site['function'], 'text': 'unreviewed'})
                        with self.assertRaises(ValueError): presentation.compose_presentation(bad, elf, rev, sections, symbols)
                bad = copy.deepcopy(sections); pc, data = bad[0]; bad[0] = pc, bytes([data[0]^1])+data[1:]
                with self.assertRaises(ValueError): presentation.compose_presentation(base, elf, rev, bad, symbols)
                owned = copy.deepcopy(base)
                text = '{ extern int dkr_legacy_character_menu(uint8_t*, recomp_context*, unsigned, const uint32_t*); if (dkr_legacy_character_menu(rdram, ctx, 8U, dkr_character_menu_fields)) return; }'
                owned['functionHooks'].append({'function':'sound_play','beforeVram':'0x80001200','text':text})
                merged = presentation.compose_presentation(owned, elf, rev, sections, symbols)
                self.assertTrue(merged['functionHooks'][0]['text'].startswith(text+' '))

    def test_sidebar_visual_and_navigation_order_match(self):
        source = (ROOT / 'runtime-recomp/src/game/runtime_ui.cpp').read_text()
        self.assertIn('kSidebarOrder{0,1,2,3,4,8,6,5,7,9,10}', source.replace(' ', ''))

    def test_custom_stage_reuses_stock_clock_and_revision_camera(self):
        # The custom early-return hook must not bypass the stock beat override.
        source = ROOT / 'runtime-recomp/src/game'
        owner = (source / 'runtime_legacy_mods.cpp').read_text()
        self.assertIn('dkr_character_select_animation_fraction,p->stage_camera', owner)
        stage = (source / 'mods/legacy_character_stage.cpp').read_text()
        self.assertLess(stage.index('calls.animation_fraction(memory.data(),&music)'),
                        stage.index('calls.animation_override(memory.data(),&music)'))
        self.assertNotIn('advance_character_select_phase', stage)  # never tick per actor
        for rev in (77, 80):
            payload = (source / f'game_payload_v{rev}.cpp').read_text()
            self.assertIn('.stage_camera = cam_get_active_camera,', payload)
            policy = json.loads((ROOT / f'runtime-recomp/dkr.us.v{rev}.recomp-policy.json').read_text())
            clocks = [h for h in policy['functionHooks']
                      if h['function'] == 'obj_loop_char_select'
                      and 'dkr_character_select_animation_fraction' in h['text']]
            self.assertEqual(len(clocks), 1)

    def test_character_menu_composes_without_replacing_native_owners(self):
        for rev in (77,80):
            fragment=json.loads((ROOT/f'runtime-recomp/legacy-character-menu.v{rev}.recomp-fragment.json').read_text())
            symbols={f['name']:{(int(f['address'],0),f['size'])} for f in fragment['fields']}
            sections=[]
            for name,body in fragment['functions'].items():
                start=int(body['vram'],0);data=bytearray(body['size'])
                for site in fragment['hooks']:
                    if site['function']==name:
                        for i,word in enumerate(site['expected']):struct.pack_into('>I',data,int(site['expectedAt'],0)-start+4*i,int(word,0))
                body['sha256']=hashlib.sha256(data).hexdigest();sections.append((start,bytes(data)));symbols[name]={(start,len(data))}
            policy=json.loads((ROOT/f'runtime-recomp/dkr.us.v{rev}.recomp-policy.json').read_text())
            # Exact resource commit already authored/validated by its own tests.
            owner=next(h for h in policy['functionHooks'] if h['function']=='charselect_assign_ai')
            owner['text']+=' extern void dkr_legacy_character_event(uint8_t*, recomp_context*, unsigned, uint32_t); '+f'dkr_legacy_character_event(rdram, ctx, 1U, {int(fragment["fields"][3]["address"],0):#x}U);'
            before=copy.deepcopy(policy)
            result=compose.compose_character_menu(policy,fragment,sections,symbols)
            self.assertEqual(policy,before);self.assertEqual(result['instructionPatches'],policy['instructionPatches'])
            for old,new in zip(policy['functionHooks'],result['functionHooks']):
                if old['function']=='charselect_assign_ai':self.assertTrue(new['text'].endswith(old['text']))
                elif old['function']=='menu_character_select_loop':self.assertTrue(new['text'].startswith(old['text']))
                else:self.assertEqual(old,new)
            self.assertEqual(len(result['functionHooks']),len(policy['functionHooks'])+7)
            with self.assertRaises(ValueError):compose.compose_character_menu(result,fragment,sections,symbols)
            for i in range(len(sections)):
                bad=copy.deepcopy(sections);data=bytearray(bad[i][1]);data[-1]^=1;bad[i]=(bad[i][0],bytes(data))
                with self.assertRaises(ValueError):compose.compose_character_menu(policy,fragment,bad,symbols)
            for site in fragment['hooks']:
                bad=copy.deepcopy(policy);bad['instructionPatches'].append({'vram':site['vram']})
                with self.assertRaises(ValueError):compose.compose_character_menu(bad,fragment,sections,symbols)
            for field in fragment['fields']:
                bad=copy.deepcopy(symbols);bad[field['name']]={(int(field['address'],0)+4,field['size'])}
                with self.assertRaises(ValueError):compose.compose_character_menu(policy,fragment,sections,bad)

    def test_character_boundaries_and_original_owners(self):
        for rev in (77,80):
            fragment=json.loads((ROOT/f'runtime-recomp/legacy-characters.v{rev}.recomp-fragment.json').read_text())
            symbols={f['name']:{(int(f['address'],0),f['size'])} for f in fragment['fields']}
            sections=[]
            for name,body in fragment['functions'].items():
                start=int(body['vram'],0);data=bytearray(body['size'])
                for site in fragment['hooks']:
                    if site['function']==name:
                        for i,word in enumerate(site['expected']):struct.pack_into('>I',data,int(site['vram'],0)-start+i*4,int(word,0))
                body['sha256']=hashlib.sha256(data).hexdigest()
                sections.append((start,bytes(data)));symbols[name]={(start,len(data))}
            policy=json.loads((ROOT/f'runtime-recomp/dkr.us.v{rev}.recomp-policy.json').read_text())
            before=copy.deepcopy(policy)
            result=compose.compose_characters(policy,fragment,sections,symbols)
            self.assertEqual(policy,before)
            self.assertEqual(result['instructionPatches'],policy['instructionPatches'])
            self.assertEqual(len(result['functionHooks']),len(policy['functionHooks'])+2)
            for old,new in zip(policy['functionHooks'],result['functionHooks']):
                if old['function']=='charselect_assign_ai':self.assertTrue(new['text'].startswith(old['text']+' '))
                else:self.assertEqual(old,new)
            with self.assertRaises(ValueError):compose.compose_characters(result,fragment,sections,symbols)
            for index in range(len(sections)):
                bad=copy.deepcopy(sections);data=bytearray(bad[index][1]);data[-1]^=1;bad[index]=(bad[index][0],bytes(data))
                with self.assertRaises(ValueError):compose.compose_characters(policy,fragment,bad,symbols)
            for site in fragment['hooks']:
                bad=copy.deepcopy(policy);bad['instructionPatches'].append({'vram':site['vram']})
                with self.assertRaises(ValueError):compose.compose_characters(bad,fragment,sections,symbols)
            for field in fragment['fields']:
                bad=copy.deepcopy(symbols);bad[field['name']]={(int(field['address'],0)+4,field['size'])}
                with self.assertRaises(ValueError):compose.compose_characters(policy,fragment,sections,bad)

    def menu_fixture(self, revision):
        fragment = json.loads((ROOT / f"runtime-recomp/legacy-track-menu.v{revision}.recomp-fragment.json").read_text())
        # No ROM dependency in CI: exercise guards with bounded synthetic
        # bodies, and keep the committed real-input hashes unchanged.
        sections = []
        symbols = {f["name"]: {(int(f["address"],0), f["size"])} for f in fragment["fields"]}
        for name, body in fragment["functions"].items():
            start = int(body["vram"],0); data = bytearray(body["size"])
            for site in fragment["hooks"]:
                if site["function"] == name:
                    for i, word in enumerate(site["expected"]):
                        struct.pack_into(">I", data, int(site["expectedAt"],0)-start+i*4, int(word,0))
            body["sha256"] = hashlib.sha256(data).hexdigest()
            sections.append((start,bytes(data))); symbols[name] = {(start,len(data))}
        policy = json.loads((ROOT / f"runtime-recomp/dkr.us.v{revision}.recomp-policy.json").read_text())
        return policy, fragment, sections, symbols

    def test_menu_preserves_native_owners_and_data_abi(self):
        for rev in (77,80):
            policy, fragment, sections, symbols = self.menu_fixture(rev)
            before = copy.deepcopy(policy)
            result = compose.compose_track_menu(policy,fragment,sections,symbols)
            self.assertEqual(policy,before)
            self.assertEqual(policy["instructionPatches"],result["instructionPatches"])
            for old, new in zip(policy["functionHooks"],result["functionHooks"]):
                if old["function"] == "load_level_game" and "gameplay_level_begin" in old["text"]:
                    self.assertTrue(new["text"].startswith(old["text"]))
                    self.assertIn("14U, dkr_legacy_fields",new["text"])
                else:
                    self.assertEqual(old,new)
            with self.assertRaises(ValueError):
                compose.compose_track_menu(result,fragment,sections,symbols)
            for old_abi in (1,2):
                bad=copy.deepcopy(fragment);bad["abi"]=old_abi
                with self.assertRaises(ValueError):compose.compose_track_menu(policy,bad,sections,symbols)
            for index in range(len(fragment["fields"])-1):
                bad=copy.deepcopy(fragment)
                bad["fields"][index],bad["fields"][index+1]=bad["fields"][index+1],bad["fields"][index]
                with self.assertRaises(ValueError):compose.compose_track_menu(policy,bad,sections,symbols)
            for index in range(len(sections)):
                bad=copy.deepcopy(sections); data=bytearray(bad[index][1]);data[-1]^=1
                bad[index]=(bad[index][0],bytes(data))
                with self.assertRaises(ValueError):compose.compose_track_menu(policy,fragment,bad,symbols)
            for site in fragment["hooks"]:
                bad=copy.deepcopy(policy);bad["instructionPatches"].append({"vram":site["vram"]})
                with self.assertRaises(ValueError):compose.compose_track_menu(bad,fragment,sections,symbols)
            for field in fragment["fields"]:
                bad=copy.deepcopy(symbols);bad[field["name"]]={(int(field["address"],0),field["size"]+4)}
                with self.assertRaises(ValueError):compose.compose_track_menu(policy,fragment,sections,bad)

    def fixture(self, revision):
        fragment = json.loads((ROOT / f"runtime-recomp/legacy-mods.v{revision}.recomp-fragment.json").read_text())
        sections = [(int(site["vram"], 0), b"".join(struct.pack(">I", int(v, 0)) for v in site["expected"]))
                    for site in fragment["sites"] + fragment["assetApis"]]
        return fragment, sections

    def test_verified_entries_compose_without_changing_existing_policy(self):
        for revision in (77, 80):
            fragment, sections = self.fixture(revision)
            policy = json.loads((ROOT / f"runtime-recomp/dkr.us.v{revision}.recomp-policy.json").read_text())
            before = copy.deepcopy(policy)
            after = compose.compose(policy, fragment, sections)
            self.assertEqual(before, policy)
            self.assertEqual(after["instructionPatches"][:-2], before["instructionPatches"])
            self.assertEqual(after["functionHooks"][:-8], before["functionHooks"])
            self.assertTrue(all(p["value"] == "0x00000000" for p in after["instructionPatches"][-2:]))
            for site, hook in zip(fragment["sites"], after["functionHooks"][-8:-6]):
                self.assertEqual(int(hook["beforeVram"], 0), int(site["vram"], 0) + 8)

    def test_every_instruction_signature_is_required(self):
        for revision in (77, 80):
            fragment, sections = self.fixture(revision)
            for index in range(len(sections)):
                for word in range(3):
                    bad = copy.deepcopy(sections)
                    data = bytearray(bad[index][1]); data[word * 4 + 3] ^= 1
                    bad[index] = (bad[index][0], bytes(data))
                    with self.subTest(revision=revision, entry=index, word=word):
                        with self.assertRaises(ValueError):
                            compose.compose({"schemaVersion": 1}, fragment, bad)

    def test_conflicting_sites_and_incomplete_coverage_fail(self):
        for revision in (77, 80):
            fragment, sections = self.fixture(revision)
            for site in fragment["sites"]:
                for offset in (0, 4, 8):
                    address = hex(int(site["vram"], 0) + offset)
                    for field, key in (("instructionPatches", "vram"), ("functionHooks", "beforeVram")):
                        with self.assertRaises(ValueError):
                            compose.compose({"schemaVersion": 1, field: [{key: address}]}, fragment, sections)
            extra = sections + [(0x80001000, sections[0][1][:4])]
            with self.assertRaises(ValueError):
                compose.compose({"schemaVersion": 1}, fragment, extra)
            for key in ("sites", "assetApis"):
                bad = copy.deepcopy(fragment);bad[key].pop()
                with self.assertRaises(ValueError):
                    compose.compose({"schemaVersion": 1}, bad, sections)
            for site in fragment["assetApis"]:
                with self.assertRaises(ValueError):
                    compose.compose({"schemaVersion": 1, "functionHooks": [{"beforeVram": site["vram"]}]}, fragment, sections)

    def test_unmodelled_branch_into_bridge_fails(self):
        for revision in (77, 80):
            fragment, sections = self.fixture(revision)
            for offset in (4, 8):
                destination = int(fragment["sites"][0]["vram"], 0) + offset
                jump = 0x08000000 | ((destination >> 2) & 0x03ffffff)
                with self.assertRaises(ValueError):
                    compose.compose({"schemaVersion": 1}, fragment, sections + [(0x80001000, struct.pack(">I", jump))])

    def test_function_bounds_and_manual_function_identity(self):
        for revision in (77, 80):
            fragment, _ = self.fixture(revision)
            symbols = {site["function"]: {(int(site["vram"], 0), 12)}
                       for site in fragment["sites"] + fragment["assetApis"]}
            symbols[fragment["callTarget"]] = {(int(fragment["callTargetVram"], 0), 12)}
            compose.verify_function_bounds({}, fragment, symbols)
            for name in symbols:
                bad = copy.deepcopy(symbols); address, size = next(iter(bad[name]))
                bad[name] = {(address + 4, size)}
                with self.assertRaises(ValueError):
                    compose.verify_function_bounds({}, fragment, bad)
            site = fragment["sites"][1]; del symbols[site["function"]]
            manual = {"name": site["function"], "vram": site["vram"], "size": "0xC"}
            compose.verify_function_bounds({"manualFunctions": [manual]}, fragment, symbols)

    def test_scene_composition_preserves_existing_owner_and_checks_signatures(self):
        for revision, level, menu, word in ((77, 0x8006B250, 0x8006E2E8, 0x81CE3514),
                                            (80, 0x8006B490, 0x8006E528, 0x81CE3A94)):
            fragment, sections = self.fixture(revision)
            sections += [(level, struct.pack(">III", 0x27BDFFA0, 0xAFBF002C, 0xAFB10028)),
                         (menu, struct.pack(">III", 0x27BDFFE0, 0x3C0E8012, word))]
            symbols = {"level_load": {(level, 64)}, "load_level_for_menu": {(menu, 64)}}
            base = json.loads((ROOT / f"runtime-recomp/dkr.us.v{revision}.recomp-policy.json").read_text())
            before = copy.deepcopy(base)
            after = compose.compose_scene_runtime(base, fragment, sections, symbols)
            self.assertEqual(base, before)
            self.assertEqual(len(after["functionHooks"]), len(base["functionHooks"]))
            for old, new in zip(base["functionHooks"], after["functionHooks"], strict=True):
                if int(old["beforeVram"], 0) == level:
                    self.assertTrue(new["text"].startswith(old["text"]))
                    self.assertEqual(new["text"].count("dkr_legacy_scene_begin(rdram, ctx)"), 1)
                else:
                    self.assertEqual(old, new)
            self.assertEqual(base["instructionPatches"], after["instructionPatches"])
            probe = compose.compose_scene_runtime(base, fragment, sections, symbols, True)
            self.assertEqual(probe["functionHooks"][:-1], after["functionHooks"])
            self.assertEqual(probe["functionHooks"][-1]["function"], "load_level_for_menu")
            with self.assertRaises(ValueError):
                compose.compose_scene_runtime(after, fragment, sections, symbols)
            for index in (-1, -2):
                for offset in range(12):
                    broken = copy.deepcopy(sections)
                    data = bytearray(broken[index][1]); data[offset] ^= 1
                    broken[index] = (broken[index][0], bytes(data))
                    with self.assertRaises(ValueError):
                        compose.compose_scene_runtime(base, fragment, broken, symbols, True)
            for name in symbols:
                broken = copy.deepcopy(symbols); broken[name].add((0x80001000, 64))
                with self.assertRaises(ValueError):
                    compose.compose_scene_runtime(base, fragment, sections, broken, True)
            conflicting = copy.deepcopy(base)
            conflicting["instructionPatches"].append({"vram": hex(menu)})
            with self.assertRaises(ValueError):
                compose.compose_scene_runtime(conflicting, fragment, sections, symbols, True)


if __name__ == "__main__":
    unittest.main()
