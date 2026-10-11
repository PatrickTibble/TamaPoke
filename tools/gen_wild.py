#!/usr/bin/env python3
"""Compile the region encounter documents into wild_areas.h (fork-only).

    python3 tools/gen_wild.py             # regenerate and print the report
    python3 tools/gen_wild.py --check     # fail if the committed header is stale
    python3 tools/gen_wild.py --selftest  # every guard must fail on a broken copy

The source is docs/<region>_region_pokemon_encounters.md and nothing else. There
is one document per REGIONS entry in dex_data.py, and the ladder (which leader
owns which tier, and the tier titles) comes from trainers.h, so this script
never restates either list.

The format contract, which editing a document must keep:
  - a section is "## N. Heading"; "### ..." subheadings inside it are ignored
    and their tables belong to the section
  - an encounter table's header cells start with Pok / Location / Level /
    Encounter; the separator row is any mix of '-' and ':'
  - "**Available Areas...:**" lines (optionally tagged, trailing '.' allowed)
    list the places a section covers
Anything this cannot read fails the build. Nothing is silently skipped, and
every override below has to match something or the build fails too.
"""
import argparse
import copy
import os
import re
import shutil
import sys
import tempfile
import unicodedata

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from dex_data import DEX, LEGENDARY, REGIONS  # noqa: E402

DOCS = os.path.join(ROOT, 'docs')
OUT = os.path.join(ROOT, 'wild_areas.h')
DOC_SUFFIX = '_region_pokemon_encounters.md'

NAME_MAX = 15                 # WILD_NAME_MAX: 15 chars at size 2 fill 180 px
MAX_BYTES = 24 * 1024
TM_M, TM_D, TM_N, TM_ALL = 1, 2, 4, 7
TIME_DEFAULT = {'MORNING': TM_M, 'DAY': TM_D, 'DAYTIME': TM_M | TM_D, 'NIGHT': TM_N}
SKIP_RATE = re.compile(r'\b(static|story|special|gift|roaming|event)\b', re.I)
KEYWORDS = [(re.compile(r'\b(very|extremely) rare\b', re.I), 10),
            (re.compile(r'\bcommon to uncommon\b', re.I), 250),
            (re.compile(r'\buncommon\b', re.I), 150),
            (re.compile(r'\brare\b', re.I), 50),
            (re.compile(r'\bcommon\b', re.I), 400)]
PCT = re.compile(r'(\d+(?:\.\d+)?)\s*%(?:\s*[\u2013-]\s*(\d+(?:\.\d+)?)\s*%)?')
LEVELS = re.compile(r'^(\d+)(?:\s*[\u2013-]\s*(\d+))?$')

# ---- Overrides: the whole record of every judgement call, per region. ------
# SECTION_TIER  section -> tier, where naming a leader gives the wrong answer
# TIER_TITLES   section -> title of the pre-Elite 4 tier (TRAINER_GYMS + 1)
# SKIP_SECTIONS legendaries, post-game, gifts and trades; only their Available
#               Areas lines are read, into the known set
# ALIASES       raw location text -> area names ([] drops it)
# EXTRA_AREAS   real places the Available Areas lines omit
# DISPLAY       area name -> a label that fits WILD_NAME_MAX
# RARITY        exact raw rarity -> replacement
# ROW_FIX       (section, raw Pokemon cell) -> replacement rows
# NAMES         raw species text -> slug, or None to drop it
# TIME_WORDS    time word -> mask
# PAIRED_TIMES  (section, raw Pokemon cell): its time note pairs with the
#               species in order
OVERRIDES = {
    'KANTO': {
        'SKIP_SECTIONS': {9, 10},
        'ALIASES': {'Water Routes': []},
    },
    'JOHTO': {
        'TIER_TITLES': {9: 'VICTORY ROAD'},
        'SKIP_SECTIONS': {10},
        'ALIASES': {'Trade': []},
        'EXTRA_AREAS': ['AZALEA TOWN'],
        'PAIRED_TIMES': {(5, "Meowth / Farfetch'd")},
    },
    'HOENN': {
        'TIER_TITLES': {9: 'VICTORY ROAD'},
        'SKIP_SECTIONS': {10, 11},
        'ALIASES': {
            'Sea Routes (Surf)': ['ROUTE 105', 'ROUTE 108'],
            'Surfing Routes': ['ROUTE 105', 'ROUTE 108'],
            'Underwater (Dive on Routes 124, 126)': ['ROUTE 124', 'ROUTE 126'],
            'Underwater (Dive areas)': ['ROUTE 124', 'ROUTE 126'],
            'All previous routes': [],
            'plus Surf-accessible areas (Route 105, Route 108, Abandoned Ship / '
            'Sea Mauville, Route 115, Route 118 east bank)':
                ['ROUTE 105', 'ROUTE 108', 'ROUTE 115', 'ROUTE 118'],
        },
        'RARITY': {'Static / Uncommon': 'Uncommon'},
    },
    'SINNOH': {
        'TIER_TITLES': {8: 'VICTORY ROAD'},
        'SKIP_SECTIONS': {9, 10},
        'ALIASES': {'Ruin Maniac Cave / Tunnel': ['MANIAC TUNNEL'],
                    'Snowpoint Temple': [],
                    'Canalave City (Surf)': []},
        'EXTRA_AREAS': ['OLD CHATEAU', 'TROPHY GARDEN'],
        'DISPLAY': {'VALLEY WINDWORKS': 'WINDWORKS',
                    'ACUITY LAKEFRONT': 'ACUITY SHORE'},
        'RARITY': {'Daily Marsh Encounter': 'Uncommon',
                   'Version Dependent': 'Common'},
        'ROW_FIX': {
            (3, 'Geodude / Hippopotas'): [
                {'pokemon': 'Geodude', 'rate': 'Common'},
                {'pokemon': 'Hippopotas', 'rate': 'Rare (5%\u201310%)'}],
            # the note sits in the Location cell, which is stripped
            (6, 'Noctowl'): [{'rate': 'Common (Night)'}],
        },
    },
    'UNOVA': {
        # §1 names Roxie, not Cheren, and tier 1 would be empty; §6 names Roxie,
        # Drayden and Marlon, and Roxie would break monotonicity
        'SECTION_TIER': {1: 1, 6: 7},
        'TIER_TITLES': {7: 'VICTORY ROAD'},
        'SKIP_SECTIONS': {8, 9},
        'ALIASES': {'Wellspring/Chargestone (Dust Clouds)': ['CHARGESTONE CAVE']},
        'EXTRA_AREAS': ['ROUTE 16', 'MISTRALTON CAVE'],
        'DISPLAY': {'CASTELIA GARDENS': 'CASTELIA PARK',
                    'DRIFTVEIL DRAWBRIDGE': 'DRAWBRIDGE',
                    'CHARGESTONE CAVE': 'CHARGESTONE',
                    'REVERSAL MOUNTAIN': 'REVERSAL MTN',
                    'DRAGONSPIRAL TOWER': 'DRAGONSPIRAL'},
        # Turtouga duplicates Tirtouga in the same row
        'NAMES': {'Grimimer': 'grimer', 'Turtouga': None},
    },
    'KALOS': {
        'TIER_TITLES': {9: 'VICTORY ROAD'},
        'SKIP_SECTIONS': {10, 11},
        'ALIASES': {'Cyllage / Ambrette / Azure Bay (Old Rod)': ['AZURE BAY']},
        'DISPLAY': {'SANTALUNE FOREST': 'SANTALUNE WOODS'},
        'RARITY': {'Horde Encounter': 'Common', 'Rock Smash': 'Uncommon'},
        'NAMES': {'Skrepler': 'skrelp'},
    },
    'ALOLA': {
        'SKIP_SECTIONS': {5, 6},
        'ALIASES': {'Poni Breaker Coast': []},
        'EXTRA_AREAS': ['HAUOLI CEMETERY', 'MOUNT LANAKILA'],
        'DISPLAY': {'WELA VOLCANO PARK': 'WELA VOLCANO',
                    'THRIFTY MEGAMART': 'MEGAMART',
                    'EXEGGUTOR ISLAND': 'EXEGGUTOR ISLE',
                    'ANCIENT PONI PATH': 'ANCIENT PATH',
                    'VAST PONI CANYON': 'PONI CANYON'},
        'RARITY': {'Overworld Chaser': 'Uncommon',
                   'Fishing Encounter': 'Common',
                   'Weather SOS Only (Sandstorm)': 'Rare',
                   'SOS Encounter from Haunter': 'Rare',
                   'SOS Encounter from Jangmo-o': 'Rare'},
        'ROW_FIX': {
            (3, 'Shuppet / Mimikyu'): [
                {'pokemon': 'Shuppet', 'rate': 'Common'},
                {'pokemon': 'Mimikyu', 'rate': 'Very Rare (5%)'}],
            (4, 'Turtonator *(Sun/US)*'): [
                {'location': 'Blush Mountain', 'level': '26\u201330'},
                {'location': 'Mount Lanakila', 'level': '42\u201346'}],
        },
        'NAMES': {'Belum': 'beldum'},
        # Sun/Moon has only day and night
        'TIME_WORDS': {'DAY': TM_M | TM_D},
    },
}
# EXTRA_AREAS is not here: it is "used" when a slot lands in it, checked apart
TABLES = ('SECTION_TIER', 'TIER_TITLES', 'SKIP_SECTIONS', 'ALIASES', 'DISPLAY',
          'RARITY', 'ROW_FIX', 'NAMES', 'TIME_WORDS', 'PAIRED_TIMES')

SLUG = {s: n for n, s, *_ in DEX}
NAME = {n: d for n, _, d, *_ in DEX}
# evolvesTo -> (base, the level it evolves at)
EVOLVES_FROM = {e: (n, lvl) for n, _, _, _, e, lvl in DEX if e}


class GenError(Exception):
    pass


def fail(msg, *a):
    raise GenError(msg % a if a else msg)


def ascii_fold(s):
    return ''.join(c for c in unicodedata.normalize('NFKD', s)
                   if not unicodedata.combining(c))


def norm(s):
    """Transliterate, never delete: 'Pokemon Tower' keeps its E."""
    s = re.sub(r'[-/\u2013\u2014&]', ' ', ascii_fold(s).upper())
    return re.sub(r'\s+', ' ', re.sub(r'[^A-Z0-9 ]', '', s)).strip()


def slugify(s):
    return re.sub(r'[^a-z0-9]', '', ascii_fold(s).lower())


def strip_parens(s):
    while True:
        t = re.sub(r'\([^()]*\)', '', s)
        if t == s:
            return re.sub(r'\s+', ' ', t).strip()
        s = t


def split0(s, sep):
    """Split on `sep` only at parenthesis depth 0."""
    out, depth, cur, i = [], 0, '', 0
    while i < len(s):
        c = s[i]
        depth += (c == '(') - (c == ')')
        if depth == 0 and s.startswith(sep, i):
            out.append(cur)
            cur, i = '', i + len(sep)
            continue
        cur += c
        i += 1
    out.append(cur)
    return [p.strip() for p in out if p.strip()]


def paren_groups(s):
    return re.findall(r'\(([^()]*)\)', s)


class Ctx:
    """One region's overrides, with a use count per entry."""

    def __init__(self, region, ov):
        self.region, self.ov, self.used = region, ov, {}
        self.words = dict(TIME_DEFAULT)
        self.words.update(ov.get('TIME_WORDS', {}))

    def get(self, table, key):
        t = self.ov.get(table, {})
        if key in t:
            self.used[(table, key)] = self.used.get((table, key), 0) + 1
            return True, (t[key] if isinstance(t, dict) else key)
        return False, None

    def unused(self):
        for table in TABLES:
            for key in self.ov.get(table, ()):
                if (table, key) not in self.used:
                    yield table, key


def time_note(ctx, text):
    """A group made only of time words joined by '/', optionally followed by a
    percentage, gives its masks in order; anything else is not a time note."""
    m = re.fullmatch(r'\s*([A-Za-z]+(?:\s*/\s*[A-Za-z]+)*)\s*(?:\d+(?:\.\d+)?\s*%)?\s*', text)
    if not m:
        return None
    toks = [w.strip().upper() for w in m.group(1).split('/')]
    hits = [w in ctx.words for w in toks]
    if not any(hits):
        return None
    if not all(hits):
        fail('%s: unknown time note "(%s)"', ctx.region, text)
    for w in toks:
        if w in ctx.ov.get('TIME_WORDS', {}):
            ctx.get('TIME_WORDS', w)
    return [ctx.words[w] for w in toks]


def resolve(ctx, cell):
    """A location cell -> area names. ALIASES is tried on the whole cell, then
    on each ',' piece, then on each ' / ' piece; a bare number continues the
    previous piece's prefix ('Route 29, 30')."""
    hit, v = ctx.get('ALIASES', cell.strip())
    if hit:
        return [norm(x) for x in v]
    out, prefix = [], None
    for piece in split0(cell, ','):
        hit, v = ctx.get('ALIASES', piece)
        if hit:
            out += [norm(x) for x in v]
            prefix = None
            continue
        for sub in split0(piece, ' / '):
            hit, v = ctx.get('ALIASES', sub)
            if hit:
                out += [norm(x) for x in v]
                prefix = None
                continue
            name = norm(strip_parens(sub))
            if not name:
                fail('%s: empty location in "%s"', ctx.region, cell)
            if name.isdigit():
                if prefix is None:
                    fail('%s: bare number "%s" with nothing to continue in "%s"',
                         ctx.region, name, cell)
                name = prefix + ' ' + name
            name = re.sub(r'^ROUTES\b', 'ROUTE', name)
            m = re.match(r'^(.*\D) \d+$', name)
            prefix = m.group(1) if m else None
            out.append(name)
    return out


def parse_doc(ctx, path):
    """-> [(number, heading, [available line], [row cells])]"""
    secs, cur, table = [], None, None
    with open(path, encoding='utf-8') as f:
        lines = f.read().splitlines()
    for ln, line in enumerate(lines, 1):
        s = line.strip()
        m = re.match(r'^##\s+(\d+)\.\s+(.*)$', s)
        if m:
            cur = (int(m.group(1)), m.group(2).strip(), [], [], [])
            secs.append(cur)
            table = None
            continue
        if cur is None:
            continue
        m = re.match(r'^\*\*Available Areas[^*]*:\*\*\s*(.*?)\.?\s*$', s)
        if m:
            cur[2].append(m.group(1))
            continue
        if not s.startswith('|'):
            table = None
            continue
        cells = [c.strip() for c in s.strip('|').split('|')]
        if table is None:
            table = 'head'
            ok = (len(cells) == 4 and all(c.startswith(p) for c, p in
                  zip(cells, ('Pok', 'Location', 'Level', 'Encounter'))))
            cur[4].append((ln, ok, cells))
            continue
        if table == 'head':
            if not all(re.fullmatch(r':?-+:?', c) for c in cells):
                fail('%s:%d: table header is not followed by a separator row', path, ln)
            table = 'rows'
            continue
        cur[3].append((ln, cells))
    return secs


def load_ladder():
    src = open(os.path.join(ROOT, 'trainers.h'), encoding='utf-8').read()
    gyms = int(re.search(r'#define TRAINER_GYMS (\d+)', src).group(1))
    m = re.search(r'TRAINER_SETS\[[^\]]*\]\s*=\s*\{(.*?)\n\};', src, re.S)
    sets = dict((r, a) for a, r in re.findall(r'\{\s*(\w+)\s*,\s*"(\w+)"\s*\}', m.group(1)))
    ladders = {}
    for region, arr in sets.items():
        body = re.search(r'static const Trainer %s\[[^\]]*\]\s*=\s*\{(.*?)\n\};' % arr,
                         src, re.S).group(1)
        rows = re.findall(r'\{\s*"([^"]+)"\s*,\s*"([^"]+)"', body)
        if len(rows) < gyms:
            fail('trainers.h: %s has %d trainers, fewer than TRAINER_GYMS', arr, len(rows))
        ladders[region] = rows[:gyms]
    return gyms, ladders


def load_noart():
    src = open(os.path.join(ROOT, 'noart.h'), encoding='utf-8').read()
    body = re.search(r'NO_ART\[[^\]]*\]\s*=\s*\{([^}]*)\}', src).group(1)
    return {int(x) for x in re.findall(r'\d+', body)} - {0}


def classify(ctx, secs, gyms, ladder, log):
    """-> {section: (tier, title or None)}; skipped sections are absent."""
    out, last = {}, 0
    for t in ('SECTION_TIER', 'TIER_TITLES', 'SKIP_SECTIONS'):
        for k in ctx.ov.get(t, ()):
            if k not in {s[0] for s in secs}:
                fail('%s: %s names section %d, which does not exist', ctx.region, t, k)
    for num, head, *_ in secs:
        if ctx.get('SKIP_SECTIONS', num)[0]:
            log.append('  section %d skipped: %s' % (num, head))
            continue
        hit, tier = ctx.get('SECTION_TIER', num)
        title = None
        if not hit:
            hit, title = ctx.get('TIER_TITLES', num)
            if hit:
                tier = gyms + 1
        if not hit:
            h = norm(head)
            named = [i for i, (n, _) in enumerate(ladder)
                     if re.search(r'(?<![A-Z0-9])' + re.escape(norm(n)), h)]
            if not named:
                fail('%s: section %d "%s" names no gym leader and has no override',
                     ctx.region, num, head)
            tier = min(named) + 1
        if tier <= last:
            fail('%s: section %d has tier %d after tier %d (tiers must increase)',
                 ctx.region, num, tier, last)
        last = tier
        out[num] = (tier, title)
    return out


def parse_rate(ctx, raw):
    """-> (weight in tenths of a percent, each, [time masks] or None)."""
    notes = [n for n in (time_note(ctx, g) for g in paren_groups(raw)) if n]
    if len(notes) > 1:
        fail('%s: two time notes in rarity "%s"', ctx.region, raw)
    m = PCT.search(raw)
    if m:
        a = float(m.group(1))
        v = (a + float(m.group(2))) / 2 if m.group(2) else a
        w = int(round(v * 10))
    else:
        w = next((v for rx, v in KEYWORDS if rx.search(raw)), None)
        if w is None:
            fail('%s: no rarity keyword or percentage in "%s"', ctx.region, raw)
    return max(1, w), bool(re.search(r'\beach\b', raw, re.I)), (notes[0] if notes else None)


def parse_names(ctx, cell, log):
    """-> [(dex, tmask from an italic tag, or None)]"""
    out = []
    for piece in split0(cell, ' / '):
        tm = None
        for tag in re.findall(r'\*\(([^)]*)\)\*', piece):
            n = time_note(ctx, tag)
            if n:
                if len(n) != 1 or tm is not None:
                    fail('%s: ambiguous species time tag in "%s"', ctx.region, cell)
                tm = n[0]
        raw = re.sub(r'\s+', ' ', re.sub(r'\*\([^)]*\)\*', '', piece)).strip()
        hit, slug = ctx.get('NAMES', raw)
        if hit and slug is None:
            log.append('  name "%s" dropped (NAMES)' % raw)
            continue
        if not hit:
            slug = slugify(raw)
            if slug not in SLUG:
                alt = slugify(strip_parens(raw))
                if alt not in SLUG:
                    fail('%s: unknown species "%s"', ctx.region, raw)
                log.append('  form "%s" -> %s' % (raw, NAME[SLUG[alt]]))
                slug = alt
        if slug not in SLUG:
            fail('%s: NAMES maps "%s" to unknown slug "%s"', ctx.region, raw, slug)
        out.append((SLUG[slug], tm))
    return out


def build_region(ctx, path, gyms, ladder, noart, log):
    secs = parse_doc(ctx, path)
    nums = [s[0] for s in secs]
    if len(set(nums)) != len(nums):
        fail('%s: duplicate section numbers', ctx.region)
    tiers = classify(ctx, secs, gyms, ladder, log)

    # known places, and the tier at which a tier section first lists each
    known, listed = set(), {}
    for num, _, avail, _, _ in secs:
        for line in avail:
            for a in resolve(ctx, line):
                known.add(a)
                if num in tiers:
                    listed[a] = min(listed.get(a, 99), tiers[num][0])
    for v in ctx.ov.get('ALIASES', {}).values():
        known.update(norm(x) for x in v)
    for a in ctx.ov.get('EXTRA_AREAS', []):
        if norm(a) in known:
            fail('%s: EXTRA_AREAS "%s" is already listed; remove the override', ctx.region, a)
        known.add(norm(a))

    rowfix = ctx.ov.get('ROW_FIX', {})
    fixhits = {k: 0 for k in rowfix}
    slots, order = [], 0
    for num, head, _, rows, tables in secs:
        if num not in tiers:
            continue
        for ln, ok, cells in tables:
            if not ok:
                fail('%s:%d: unreadable table header %s', path, ln, cells)
        tier = tiers[num][0]
        for ln, cells in rows:
            if len(cells) != 4:
                fail('%s:%d: expected 4 cells, got %d', path, ln, len(cells))
            pkm = cells[0].replace('**', '').strip()
            fixed = [dict(zip(('pokemon', 'location', 'level', 'rate'), cells))]
            if (num, pkm) in rowfix:
                fixhits[(num, pkm)] += 1
                ctx.get('ROW_FIX', (num, pkm))
                fixed = [dict(fixed[0], **r) for r in rowfix[(num, pkm)]]
                log.append('  ROW_FIX %d "%s" -> %d row(s)' % (num, pkm, len(fixed)))
            for row in fixed:
                order = add_row(ctx, path, ln, num, tier, row, noart, known, slots, order, log)
    for k, n in fixhits.items():
        if n > 1:
            fail('%s: ROW_FIX %s matched %d rows, expected exactly one', ctx.region, k, n)

    # areas exist only where slots do
    areas = {}
    for s in slots:
        a = areas.setdefault(s['area'], {'first': s['order'], 'slot_tier': 99})
        a['slot_tier'] = min(a['slot_tier'], s['tier'])
    for name, a in areas.items():
        a['tier'] = max(listed.get(name, 0), a['slot_tier'])
        hit, label = ctx.get('DISPLAY', name)
        a['label'] = label if hit else name
        if len(a['label']) > NAME_MAX:
            fail('%s: area "%s" is %d chars, over WILD_NAME_MAX %d (add a DISPLAY entry)',
                 ctx.region, a['label'], len(a['label']), NAME_MAX)
    if not any(a['tier'] == 1 for a in areas.values()):
        fail('%s: tier 1 owns no area, so the region opens with nothing playable', ctx.region)

    # merge duplicates: lower tier, higher weight
    merged, seen = [], {}
    for s in slots:
        k = (s['area'], s['dex'], s['lo'], s['hi'], s['tmask'])
        if k in seen:
            m = seen[k]
            log.append('  merged %s %s %d-%d (tiers %d/%d)' % (
                s['area'], NAME[s['dex']], s['lo'], s['hi'], m['tier'], s['tier']))
            m['tier'], m['weight'] = min(m['tier'], s['tier']), max(m['weight'], s['weight'])
            continue
        seen[k] = s
        merged.append(s)

    titles = {}
    for a in areas.values():
        t = a['tier']
        if t not in titles:
            if t <= gyms:
                titles[t] = norm(ladder[t - 1][1])
            else:
                titles[t] = next(title for tt, title in tiers.values() if tt == t)
            if len(titles[t]) > NAME_MAX:
                fail('%s: tier title "%s" is over WILD_NAME_MAX', ctx.region, titles[t])

    for table, key in ctx.unused():
        fail('%s: override %s[%r] matched nothing', ctx.region, table, key)
    for a in ctx.ov.get('EXTRA_AREAS', []):
        if norm(a) not in areas:
            fail('%s: EXTRA_AREAS "%s" received no slot', ctx.region, a)
    return areas, merged, titles


def add_row(ctx, path, ln, num, tier, row, noart, known, slots, order, log):
    where = '%s §%d "%s"' % (ctx.region, num, row['pokemon'])
    hit, rate = ctx.get('RARITY', row['rate'].strip())
    rate = rate if hit else row['rate'].strip()
    if SKIP_RATE.search(rate):
        log.append('  skip %s: rarity "%s"' % (where, rate))
        return order
    species = parse_names(ctx, row['pokemon'].replace('**', '').strip(), log)
    leg = [d for d, _ in species if d in LEGENDARY]
    if leg:
        log.append('  skip %s: legendary %s' % (where, NAME[leg[0]]))
        return order

    weight, each, note = parse_rate(ctx, rate)
    tags = [tm for _, tm in species if tm is not None]
    paired, _ = ctx.get('PAIRED_TIMES', (num, row['pokemon'].replace('**', '').strip()))
    if tags and note:
        fail('%s: both a species time tag and a rarity time note', where)
    if paired:
        if not note or len(note) != len(species):
            fail('%s: PAIRED_TIMES needs one time word per species', where)
        species = [(d, m) for (d, _), m in zip(species, note)]
    else:
        mask = 0
        for m in note or []:
            mask |= m
        species = [(d, tm if tm is not None else (mask or TM_ALL)) for d, tm in species]
    if not tags and not note and any(time_note(ctx, g) for g in paren_groups(row['location'])):
        fail('%s: a time note sits only in the Location cell; move it with ROW_FIX', where)

    m = LEVELS.match(row['level'].strip())
    if not m:
        fail('%s: unreadable level "%s"', where, row['level'])
    lo = int(m.group(1))
    hi = int(m.group(2)) if m.group(2) else lo
    if not (1 <= lo <= hi <= 100):
        fail('%s: level %d-%d outside 1..100 or reversed', where, lo, hi)

    areas = resolve(ctx, row['location'])
    if not areas:
        fail('%s: every location dropped away (%s)', where, row['location'])
    for a in areas:
        if a not in known:
            fail('%s: unknown location "%s"', where, a)

    kept = []
    for d, tm in species:
        if d in noart:
            log.append('  art-less %s dropped from %s' % (NAME[d], where))
        else:
            kept.append((d, tm))
    if not kept:
        log.append('  row %s emptied by the art-less drop' % where)
        return order

    # only the evolved form of a pair is clamped; the base keeps the full range
    present = {d for d, _ in kept}
    ranged = []
    for d, tm in kept:
        base = EVOLVES_FROM.get(d)
        if base and base[0] in present:
            elo = max(lo, base[1])
            if elo > hi:
                log.append('  %s dropped from %s: evolves at %d, row tops out at %d'
                           % (NAME[d], where, base[1], hi))
                continue
            if elo != lo:
                log.append('  %s clamped to %d-%d in %s' % (NAME[d], elo, hi, where))
            ranged.append((d, tm, elo))
        else:
            ranged.append((d, tm, lo))
    share = weight if each else max(1, weight // len(ranged))
    for d, tm, slo in ranged:
        for a in areas:
            slots.append({'area': a, 'dex': d, 'weight': share, 'tier': tier,
                          'lo': slo, 'hi': hi, 'tmask': tm, 'order': order})
            order += 1
    return order


def build(docs=DOCS, overrides=None, noart=None):
    """-> (header text, report lines). Raises GenError on anything unreadable."""
    overrides = OVERRIDES if overrides is None else overrides
    noart = load_noart() if noart is None else noart
    gyms, ladders = load_ladder()
    names = [r[0] for r in REGIONS]
    for f in sorted(os.listdir(docs)):
        if f.endswith(DOC_SUFFIX) and f[:-len(DOC_SUFFIX)].upper() not in names:
            fail('docs/%s maps to no region in dex_data.REGIONS', f)
    for r in overrides:
        if r not in names:
            fail('OVERRIDES has region %s, which is not in REGIONS', r)

    regions = []
    for region in names:
        path = os.path.join(docs, region.lower() + DOC_SUFFIX)
        if not os.path.exists(path):
            fail('missing document for %s: %s', region, path)
        if region not in ladders:
            fail('%s has no TRAINER_SETS entry in trainers.h', region)
        ctx = Ctx(region, overrides.get(region, {}))
        log = []
        areas, slots, titles = build_region(ctx, path, gyms, ladders[region], noart, log)
        regions.append((region, areas, slots, titles, log))
    return emit(regions, gyms)


def emit(regions, gyms):
    areas_out, slots_out, tiers_out, span_a, span_s, report = [], [], [], [], [], []
    for idx, (region, areas, slots, titles, log) in enumerate(regions):
        names = sorted(areas, key=lambda n: (areas[n]['tier'], areas[n]['first']))
        gidx = {n: len(areas_out) + i for i, n in enumerate(names)}
        span_a.append((len(areas_out), len(names)))
        first_slot = len(slots_out)
        report.append('%s: %d areas, %d slots' % (region, len(names), len(slots)))
        for n in names:
            a = areas[n]
            mine = [s for s in slots if s['area'] == n]
            areas_out.append((idx, a['tier'], a['label'], region, n))
            for s in mine:
                slots_out.append((gidx[n], s, a['label']))
        for t in sorted(titles):
            tiers_out.append((idx, t, titles[t]))
            row = ['%s(%d)' % (areas[n]['label'], sum(s['area'] == n for s in slots))
                   for n in names if areas[n]['tier'] == t]
            report.append('  tier %d %-12s %s' % (t, titles[t], ', '.join(row)))
        span_s.append((first_slot, len(slots_out) - first_slot))
        report += log

    size = (len(slots_out) * 10 + len(areas_out) * 8 + len(tiers_out) * 8 + 2 * 8 * 4
            + sum(len(a[2]) + 1 for a in areas_out) + sum(len(t[2]) + 1 for t in tiers_out))
    report.append('table: %d areas, %d slots, %d tiers, about %d bytes of flash'
                  % (len(areas_out), len(slots_out), len(tiers_out), size))
    if size > MAX_BYTES:
        fail('the table is %d bytes, over the %d byte limit', size, MAX_BYTES)

    n_reg = len(regions)
    h = ['// GENERATED by tools/gen_wild.py from docs/*%s. Do not edit:' % DOC_SUFFIX,
         '// change the document (or an override in gen_wild.py) and re-run it.',
         '//',
         '// Included by wild.cpp ONLY. Every array is static const, so a second',
         '// translation unit including this would put a second copy in flash; the',
         '// sketch and the tests reach the table through the accessors in wild.h.',
         '#pragma once',
         '#include <stdint.h>',
         '#include "dex.h"',
         '',
         'static_assert(REGION_COUNT == %d && REGION_ALL == %d,' % (n_reg + 1, n_reg),
         '              "wild_areas.h is stale: re-run tools/gen_wild.py");',
         '',
         '#define WILD_NAME_MAX %d' % NAME_MAX,
         '#define WILD_TM_MORNING %d' % TM_M,
         '#define WILD_TM_DAY %d' % TM_D,
         '#define WILD_TM_NIGHT %d' % TM_N,
         '#define WILD_TM_ALL %d' % TM_ALL,
         '',
         'struct WildArea { uint8_t region, tier; const char *name; };',
         'struct WildSlot { uint16_t area; int16_t dex; uint16_t weight; uint8_t tier, lo, hi, tmask; };',
         'struct WildTier { uint8_t region, tier; const char *title; };',
         'struct WildSpan { uint16_t first, count; };',
         '',
         '#define WILD_AREA_COUNT %d' % len(areas_out),
         'static const WildArea WILD_AREAS[WILD_AREA_COUNT] = {']
    last = None
    for i, (r, t, label, region, _) in enumerate(areas_out):
        if region != last:
            h.append('  // %s' % region)
            last = region
        h.append('  { %d, %d, "%s" },  // %d' % (r, t, label, i))
    h += ['};', '', '#define WILD_SLOT_COUNT %d' % len(slots_out),
          '// { area, dex, weight in tenths of a percent, tier, lo, hi, tmask }',
          'static const WildSlot WILD_SLOTS[WILD_SLOT_COUNT] = {']
    for a, s, label in slots_out:
        h.append('  { %d, %d, %d, %d, %d, %d, %d },  // %s: %s' % (
            a, s['dex'], s['weight'], s['tier'], s['lo'], s['hi'], s['tmask'],
            label, NAME[s['dex']]))
    h += ['};', '', '#define WILD_TIER_COUNT %d' % len(tiers_out),
          'static const WildTier WILD_TIERS[WILD_TIER_COUNT] = {']
    h += ['  { %d, %d, "%s" },' % t for t in tiers_out]
    h += ['};', '', '// indexed by region; REGION_ALL owns nothing']
    for name, spans in (('WILD_REGION_AREAS', span_a), ('WILD_REGION_SLOTS', span_s)):
        h.append('static const WildSpan %s[REGION_COUNT] = {' % name)
        h += ['  { %d, %d },  // %s' % (f, c, regions[i][0]) for i, (f, c) in enumerate(spans)]
        h += ['  { 0, 0 },  // ALL', '};']
    return '\n'.join(h) + '\n', report


def read_committed():
    if not os.path.exists(OUT):
        return None
    with open(OUT, encoding='utf-8', newline='') as f:
        return f.read()


def stale(committed, generated):
    """A CRLF checkout is not stale; anything else that differs is."""
    return committed is None or committed.replace('\r\n', '\n') != generated


# ---- --selftest ------------------------------------------------------------

def _edit_doc(region, old, new):
    def go(docs, ov, noart):
        p = os.path.join(docs, region.lower() + DOC_SUFFIX)
        src = open(p, encoding='utf-8').read()
        if src.count(old) != 1:
            # an anchor that no longer matches would make the fixture a no-op
            raise AssertionError('fixture anchor %r matches %d times' % (old, src.count(old)))
        open(p, 'w', encoding='utf-8', newline='\n').write(src.replace(old, new))
    return go


def _edit_ov(region, table, key, value=None, remove=False):
    def go(docs, ov, noart):
        t = ov[region].setdefault(table, {})
        if remove:
            if key not in t:
                raise AssertionError('fixture: %s %s has no %r' % (region, table, key))
            if isinstance(t, set):
                t.discard(key)
            else:
                del t[key]
        else:
            t[key] = value
    return go


def _delete_doc(region):
    def go(docs, ov, noart):
        os.remove(os.path.join(docs, region.lower() + DOC_SUFFIX))
    return go


MUST_FAIL = [
    ('a misspelled location', 'unknown location',
     _edit_doc('KANTO', '| **Pikachu** | Viridian Forest |', '| **Pikachu** | Viridan Forest |')),
    ('a misspelled name', 'unknown species',
     _edit_doc('KANTO', '| **Pikachu** | Viridian Forest |', '| **Pikachoo** | Viridian Forest |')),
    ('a misspelled leader', 'names no gym leader',
     _edit_doc('KANTO', '## 1. Brock (Pewter', '## 1. Brok (Pewter')),
    ('Unova §6 SECTION_TIER removed', 'tiers must increase',
     _edit_ov('UNOVA', 'SECTION_TIER', 6, remove=True)),
    ('Unova §1 SECTION_TIER removed', 'tier 1 owns no area',
     _edit_ov('UNOVA', 'SECTION_TIER', 1, remove=True)),
    ('a removed ALIASES entry', 'unknown location',
     _edit_ov('KANTO', 'ALIASES', 'Water Routes', remove=True)),
    ('an ALIASES entry that matches nothing', 'matched nothing',
     _edit_ov('KANTO', 'ALIASES', 'Nowhere Lane', ['ROUTE 1'])),
    ('a row whose only location is aliased away', 'every location dropped away',
     _edit_ov('KANTO', 'ALIASES', 'Safari Zone (Super Rod)', [])),
    ('a removed DISPLAY entry', 'over WILD_NAME_MAX',
     _edit_ov('SINNOH', 'DISPLAY', 'VALLEY WINDWORKS', remove=True)),
    ('a deleted document', 'missing document',
     _delete_doc('KALOS')),
]


def _run(fixture):
    tmp = tempfile.mkdtemp(prefix='gen_wild_')
    try:
        docs = os.path.join(tmp, 'docs')
        os.mkdir(docs)
        for f in os.listdir(DOCS):
            if f.endswith(DOC_SUFFIX):
                shutil.copy(os.path.join(DOCS, f), docs)
        ov, noart = copy.deepcopy(OVERRIDES), load_noart()
        if fixture:
            fixture(docs, ov, noart)
        return build(docs, ov, noart)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def selftest():
    bad = 0
    for what, expect, fx in MUST_FAIL:
        try:
            _run(fx)
            print('FAIL  %s: the build passed' % what)
            bad += 1
        except GenError as e:
            if expect in str(e):
                print('PASS  %s fails: %s' % (what, e))
            else:
                print('FAIL  %s failed for the wrong reason: %s' % (what, e))
                bad += 1

    # must NOT fail: the ordering rules hold on the real documents
    text, _ = _run(None)
    for what, needle in [
            ("Lugia's '45 / 70' in a parsed tier is skipped before it is parsed",
             '{ 1, 9, "VICTORY ROAD" }'),
            ('"Underwater (Dive on Routes 124, 126)" resolves through ALIASES',
             '// ROUTE 126: LANTURN'),
            ('"Lycanroc (Midday / Midnight)" is one species', '// PONI CANYON: LYCANROC')]:
        ok = needle in text
        print('%s  %s' % ('PASS' if ok else 'FAIL', what))
        bad += not ok

    # --check sees a hand edit and a new art-less species, and not line endings
    noart_plus, _ = _run(lambda docs, ov, noart: noart.add(16))
    for what, ok in [
            ('--check passes on the header it just generated', not stale(text, text)),
            ('--check passes on a CRLF checkout', not stale(text.replace('\n', '\r\n'), text)),
            ('--check fails after a hand edit to the header',
             stale(text.replace('"ROUTE 1"', '"ROUTE 1X"', 1), text)),
            ('--check fails after adding a dex number to NO_ART',
             stale(text, noart_plus) and '// ROUTE 1: PIDGEY' not in noart_plus)]:
        print('%s  %s' % ('PASS' if ok else 'FAIL', what))
        bad += not ok
    print('selftest: %s' % ('all good' if not bad else '%d failed' % bad))
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='fail if wild_areas.h is stale')
    ap.add_argument('--selftest', action='store_true', help='run the guard fixtures')
    args = ap.parse_args()
    try:
        if args.selftest:
            return selftest()
        text, report = build()
    except GenError as e:
        print('gen_wild: %s' % e, file=sys.stderr)
        return 1
    if args.check:
        if stale(read_committed(), text):
            print('gen_wild: wild_areas.h is stale -- run python3 tools/gen_wild.py',
                  file=sys.stderr)
            return 1
        return 0
    print('\n'.join(report))
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    print('wrote %s' % OUT)
    return 0


if __name__ == '__main__':
    sys.exit(main())
