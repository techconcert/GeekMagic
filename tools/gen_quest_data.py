#!/usr/bin/env python3
"""Generates lib/gamecore/src/quest_data.h and text_data.h (chunk 5, design 4.2 / 11.7).

Edit the content below, run `python3 tools/gen_quest_data.py`, commit the two headers.
Rules checked here (and again by test/test_quest.cpp): upper case, font glyphs only,
`|` is the line break, at most 2 lines of 19 characters, bands at most 14, titles at most 20.
"""
import pathlib, re, sys

OUT = pathlib.Path(__file__).resolve().parent.parent / "lib/gamecore/src"

NPC = dict(HERO=0, MIRA=1, BRAM=2, PIP=3, ODDA=4, FINN=5, TESS=6, LUMA=7, WREN=8, MISO=9)
NPC_NAMES = ["HERO", "MIRA", "BRAM", "PIP", "ODDA", "FINN", "TESS", "LUMA", "WREN", "MISO"]
AREA = dict(VILLAGE=0, FOREST=1, SHORE=2, CAVE=3, RUINS=4, HOME=5, SHOP=6, TAVERN=7)
ITEM = dict(WOOD=0, STONE=1, FISH=2, MUSHROOM=3, BERRY=4, FLOWER=5, SHELL=6, ORE=7, GEM=8, CROP=9,
            SEED=10, HERB=11, GEL=12, MAP=16, KEY=17, SHARD=18, PARCEL=19, ROPE=20, CHARM=21, OIL=22)
MON = dict(SLIME=0, BAT=1, CRAB=2, GHOST=3, BOSS=4)
ACT = dict(FISH=3, CHOP=2)
TOOL = dict(AXE=0, ROD=1, PICK=2, PURSE=3, BOOTS=4, LANTERN=5, SWORD=6)
PROJ = dict(BOARD=0, GARDEN=1, BRIDGE=2, WELL=3, STALL=4, LANTERNS=5, FLOWERBEDS=6, BELL=7, STAGE=8, FOUNTAIN=9)

def friend(npc, lvl):  # lvl 1 = 25, 2 = 50
    return ("QR_FRIEND", (NPC[npc] << 4) | lvl)

NONE = ("QR_NONE", 0)

# step: (type, a, b, c, start_text, band_text)
def TALK(npc, text, band, give=0): return ("QS_TALK", NPC[npc], give, 0, text, band)
def FETCH(item, n, text, band): return ("QS_FETCH", ITEM[item], n, 0, text, band)
def DELIVER(npc, item, n, text, band): return ("QS_DELIVER", NPC[npc], ITEM[item], n, text, band)
def GOTO(area, x, y, text, band): return ("QS_GOTO", AREA[area], x, y, text, band)
def SLAY(mon, n, area, text, band): return ("QS_SLAY", MON[mon], n, AREA[area], text, band)
def DO(act, n, text, band, noon=0): return ("QS_DO", ACT[act], n, noon, text, band)
def WAIT(text, band): return ("QS_WAIT_DAY", 0, 0, 0, text, band)
def SCENE(sid, text, band, fest=1): return ("QS_SCENE", sid, 0, fest, text, band)

# quest: (title, giver, req, coins, item or None, steps)
QUESTS = []
def Q(title, giver, req, coins, item, steps):
    QUESTS.append((title, NPC[giver], req, coins // 5, ITEM[item] if item else 0xFF, steps))

# ---- story (ids 0..5). Day and need of each chapter are in STORY_GATE.
Q("A QUIET BELL", "BRAM", NONE, 20, "MAP", [
    TALK("BRAM", "THE EVENBELL IS|SILENT. HAVE A LOOK", "ASK BRAM"),
    GOTO("VILLAGE", 7, 6, "I'LL GO SEE THE|BELL TOWER.", "SEE THE BELL"),
    TALK("BRAM", "THE CLAPPER IS|GONE. FOUR SHARDS.", "TELL BRAM"),
])
Q("SPRING SHARD", "LUMA", NONE, 40, "CHARM", [
    TALK("LUMA", "A SHARD SLEEPS IN|THE FOREST. HELP?", "ASK LUMA"),
    FETCH("HERB", 3, "HERBS FOR LUMA'S|TEA FIRST.", "HERBS"),
    GOTO("FOREST", 10, 6, "TO THE OLD|CLEARING.", "THE CLEARING"),
    SLAY("SLIME", 3, "FOREST", "SLIMES GUARD THE|SHARD!", "SLIMES"),
    TALK("LUMA", "YOU FOUND IT!|SPRING IS SAVED.", "SHOW LUMA"),
])
Q("SUMMER SHARD", "FINN", NONE, 60, None, [
    TALK("FINN", "SOMETHING SHINY|FELL IN THE SURF.", "ASK FINN"),
    FETCH("SHELL", 4, "SHELLS HIDE THE|WAY. GATHER 4.", "SHELLS"),
    WAIT("THE TIDE IS LOW|TOMORROW.", "WAIT A DAY"),
    SLAY("CRAB", 3, "SHORE", "CRABS GUARD THE|SHARD!", "CRABS"),
    TALK("FINN", "SUMMER'S SHARD!|YOU ARE A STAR.", "SHOW FINN"),
])
Q("AUTUMN SHARD", "TESS", NONE, 80, None, [
    TALK("TESS", "THE CAVE HUMS.|A SHARD, I BET.", "ASK TESS"),
    FETCH("STONE", 5, "STONE FOR PROPS.|5 SHOULD DO.", "STONE"),
    GOTO("CAVE", 7, 10, "INTO THE DARK|WE GO.", "THE CAVE"),
    SLAY("BAT", 3, "CAVE", "BATS! SO MANY|BATS!", "BATS"),
    TALK("TESS", "AUTUMN'S SHARD.|NICE WORK!", "SHOW TESS"),
])
Q("WINTER SHARD", "BRAM", NONE, 100, None, [
    TALK("BRAM", "THE LAST SHARD IS|IN THE RUINS.", "ASK BRAM"),
    GOTO("RUINS", 7, 7, "THE RUINS ARE|COLD AND QUIET.", "THE RUINS"),
    SLAY("GHOST", 3, "RUINS", "GHOSTS BLOCK THE|WAY IN.", "GHOSTS"),
    WAIT("REST FIRST. THE|BOSS CAN WAIT.", "REST A DAY"),
    SLAY("BOSS", 1, "RUINS", "MOSSBACK WAKES!|HERE IT COMES.", "MOSSBACK"),
    TALK("BRAM", "ALL FOUR SHARDS.|THE BELL WILL RING!", "SHOW BRAM"),
])
Q("THE EVENBELL RINGS", "BRAM", NONE, 150, None, [
    TALK("FINN", "HERE, ROPE FOR THE|BELL. GOOD LUCK!", "ROPE FROM FINN", give=ITEM["ROPE"]),
    DELIVER("TESS", "ROPE", 1, "ROPE! PERFECT.|HANG THE CLAPPER.", "ROPE TO TESS"),
    SCENE(1, "THE WHOLE VILLAGE|GATHERS AT DUSK.", "RING THE BELL"),
    TALK("BRAM", "DING! WE HEARD IT.|THANK YOU, HERO.", "THANK BRAM"),
])

# ---- side quests (ids 6..19): unlocked by friendship 25 and 50
Q("SHELVES FOR MIRA", "MIRA", friend("MIRA", 1), 15, None, [
    TALK("MIRA", "MY SHELVES ARE|EMPTY. WOOD?", "ASK MIRA"),
    FETCH("WOOD", 8, "8 WOOD FOR MIRA'S|SHELVES.", "WOOD"),
    TALK("MIRA", "PERFECT FIT!|THANK YOU SO MUCH.", "SEE MIRA"),
])
Q("MIRA'S SUPPER", "MIRA", friend("MIRA", 2), 25, None, [
    TALK("MIRA", "I'D LOVE A FISH|DINNER TONIGHT.", "ASK MIRA"),
    DELIVER("MIRA", "FISH", 4, "FOUR FISH! A FEAST.|HERE'S YOUR PAY.", "FISH FOR MIRA"),
])
Q("PIP VS SLIMES", "PIP", friend("PIP", 1), 10, None, [
    TALK("PIP", "SLIMES ATE MY|SNACK! HELP!", "ASK PIP"),
    SLAY("SLIME", 2, "FOREST", "TO THE FOREST FOR|SLIME DUTY.", "SLIMES"),
    TALK("PIP", "YOU'RE SO BRAVE!|CAN I COME NEXT?", "SEE PIP"),
])
Q("PIP'S GOO", "PIP", friend("PIP", 2), 15, None, [
    TALK("PIP", "I WANT SLIME GOO.|FOR SCIENCE!", "ASK PIP"),
    DELIVER("PIP", "GEL", 3, "EW! SO GOOEY!|THANKS, FRIEND!", "GOO FOR PIP"),
])
Q("ODDA'S STEW", "ODDA", friend("ODDA", 1), 15, None, [
    TALK("ODDA", "STEW NEEDS FISH.|CATCH 3, PLEASE?", "ASK ODDA"),
    FETCH("FISH", 3, "3 FISH FOR ODDA'S|STEW.", "FISH"),
    TALK("ODDA", "SMELLS WONDERFUL.|SIT, EAT A BIT!", "SEE ODDA"),
])
Q("ODDA'S TEA", "ODDA", friend("ODDA", 2), 25, None, [
    TALK("ODDA", "MY TEA IS BLAND.|HERBS WOULD HELP.", "ASK ODDA"),
    DELIVER("ODDA", "HERB", 4, "SWEET AND GREEN!|TEA'S ON THE HOUSE.", "HERBS FOR ODDA"),
])
Q("FINN'S LESSON", "FINN", friend("FINN", 1), 15, None, [
    TALK("FINN", "CATCH 3 FISH FOR|ME. I'LL WATCH.", "ASK FINN"),
    DO("FISH", 3, "FINN IS WATCHING.|NO PRESSURE.", "FISH"),
    TALK("FINN", "NOT BAD AT ALL!|YOU HAVE THE KNACK.", "SEE FINN"),
])
Q("FINN'S SHELLS", "FINN", friend("FINN", 2), 25, None, [
    TALK("FINN", "SHELLS ARE MY|LUCKY CHARMS.", "ASK FINN"),
    DELIVER("FINN", "SHELL", 4, "LOVELY! THE SEA|SMILES ON YOU.", "FINN'S SHELLS"),
])
Q("TESS'S TIMBER", "TESS", friend("TESS", 1), 15, None, [
    TALK("TESS", "I NEED 10 LOGS|FOR THE FORGE.", "ASK TESS"),
    FETCH("WOOD", 10, "10 LOGS FOR TESS.|SIMPLE ENOUGH.", "WOOD"),
    TALK("TESS", "SOLID OAK! THANKS.|COME BACK ANYTIME.", "SEE TESS"),
])
Q("TESS'S ROCKS", "TESS", friend("TESS", 2), 30, None, [
    TALK("TESS", "STONE FOR A NEW|ANVIL. 5 PIECES.", "ASK TESS"),
    DELIVER("TESS", "STONE", 5, "HEAVY AND TRUE.|HERE'S YOUR PAY.", "STONE FOR TESS"),
])
Q("LUMA'S TEA", "LUMA", friend("LUMA", 1), 15, None, [
    TALK("LUMA", "I RUN LOW ON HERBS.|FIVE, PLEASE?", "ASK LUMA"),
    FETCH("HERB", 5, "FIVE HERBS FOR|LUMA.", "HERBS"),
    TALK("LUMA", "THE FOREST THANKS|YOU.", "SEE LUMA"),
])
Q("LUMA'S GOO", "LUMA", friend("LUMA", 2), 25, None, [
    TALK("LUMA", "SLIME GEL MAKES A|FINE SALVE.", "ASK LUMA"),
    DELIVER("LUMA", "GEL", 4, "GOOD GEL! I WILL|MAKE SALVE.", "GEL FOR LUMA"),
])
Q("MISO'S SNACK", "MISO", friend("MISO", 1), 10, None, [
    TALK("MISO", "MRRRROW?|(MISO WANTS FISH)", "ASK MISO"),
    FETCH("FISH", 2, "TWO FISH FOR THE|CAT. EASY.", "FISH"),
    TALK("MISO", "PURRRR...|(THE CAT LIKES YOU)", "SEE MISO"),
])
Q("MISO'S WALK", "MISO", friend("MISO", 2), 10, None, [
    TALK("MISO", "MRRP! MRRP!|(FOLLOW ME?)", "ASK MISO"),
    GOTO("VILLAGE", 12, 6, "FOLLOWING MISO TO|HER SECRET SPOT.", "FOLLOW MISO"),
])

# ---- errand templates (ids 20..27): drawn each morning once the notice board exists
Q("WOOD FOR MIRA", "MIRA", NONE, 0, None, [
    DELIVER("MIRA", "WOOD", 5, "THANKS! WOOD IS|ALWAYS NEEDED.", "WOOD FOR MIRA")])
Q("FISH FOR ODDA", "ODDA", NONE, 0, None, [
    DELIVER("ODDA", "FISH", 3, "FRESH FISH! THE|STEW SAYS THANKS.", "FISH FOR ODDA")])
Q("PARCEL FOR FINN", "MIRA", ("QR_PROJECT", PROJ["BRIDGE"]), 0, None, [
    DELIVER("FINN", "PARCEL", 1, "A PARCEL FOR ME?|HOW NICE!", "PARCEL: FINN")])
Q("CLEAR SLIMES", "LUMA", NONE, 0, None, [
    SLAY("SLIME", 3, "FOREST", "SLIMES AGAIN!|THREE SHOULD DO.", "SLIMES")])
Q("MORNING FISH", "ODDA", NONE, 0, None, [
    DO("FISH", 1, "CATCH A FISH|BEFORE NOON!", "FISH BY NOON", noon=1)])
Q("HERBS FOR LUMA", "LUMA", NONE, 0, None, [
    DELIVER("LUMA", "HERB", 4, "THESE ARE FRESH!|MANY THANKS.", "HERBS 4 LUMA")])
Q("CLEAR CRABS", "FINN", ("QR_PROJECT", PROJ["BRIDGE"]), 0, None, [
    SLAY("CRAB", 2, "SHORE", "CRABS ON THE DOCK.|TWO, PLEASE.", "CRABS")])
Q("STONE FOR TESS", "TESS", ("QR_TOOL", (TOOL["PICK"] << 4) | 1), 0, None, [
    DELIVER("TESS", "STONE", 3, "3 STONES. JUST|ENOUGH. THANKS.", "STONE 4 TESS")])

# ---- legends (ids 28..31): from year 2, one per season; count 0 = grows with the year
Q("LEGEND OF SPRING", "LUMA", ("QR_SEASON", 0), 50, None, [
    SLAY("SLIME", 0, "FOREST", "THE SLIME KING|STIRS AGAIN!", "SLIME KING")])
Q("LEGEND OF SUMMER", "FINN", ("QR_SEASON", 1), 50, None, [
    SLAY("CRAB", 0, "SHORE", "THE GIANT CRAB|RETURNS!", "GIANT CRAB")])
Q("LEGEND OF AUTUMN", "TESS", ("QR_SEASON", 2), 50, None, [
    SLAY("BAT", 0, "CAVE", "THE BAT QUEEN|SCREECHES!", "BAT QUEEN")])
Q("LEGEND OF WINTER", "BRAM", ("QR_SEASON", 3), 50, None, [
    SLAY("BOSS", 0, "RUINS", "MOSSBACK GROWS|STRONGER!", "MOSSBACK")])

# (earliest day, need kind 0 none / 1 tool / 2 project, need value)
STORY_GATE = [
    (1, 0, 0),
    (4, 1, (TOOL["AXE"] << 4) | 1),
    (9, 2, PROJ["BRIDGE"]),
    (15, 1, (TOOL["PICK"] << 4) | 1),
    (21, 1, (TOOL["LANTERN"] << 4) | 1),
    (27, 2, PROJ["BELL"]),
]

THANKS = ["THANK YOU!|YOU'RE A HERO.", "I OWE YOU ONE!", "WELL DONE!|JUST WHAT I NEED.", "BLESS YOU!|TRUE FRIEND.",
          "WONDERFUL! TAKE|YOUR REWARD.", "THE VILLAGE IS|LUCKY TO HAVE YOU.", "YOU DID IT AGAIN!", "MANY THANKS,|BRAVE HERO!"]
REPLIES = ["I SEE!", "REALLY? NICE!", "GOOD TO KNOW.", "HA! TRUE.", "LOVELY DAY!", "I'LL REMEMBER.",
           "THANKS FOR THAT.", "WELL, WELL!"]
MISC = {"OUCH": "OUCH! HOME I GO.", "DING": "DING!"}

# Ambient lines per NPC 1..9: 10 general, 2 spring, 2 summer, 2 autumn, 2 winter, 2 rain, 2 per friendship tier (25/50/75)
AMBIENT = {
"MIRA": [
 "WOOD SELLS WELL|THIS WEEK.", "FRESH FISH? MY|FAVOURITE!", "MIND THE STEP,|THE FLOOR IS WET.",
 "COINS COME AND GO.|BUT NOT HERE!", "I COUNT THE TILL|TWICE A DAY.", "NEW STOCK SOON,|I PROMISE.",
 "A SHOP NEEDS GOOD|NEIGHBOURS.", "TRY THE SHOP|CANDLES. SO WARM!", "CAN'T TALK LONG.|CUSTOMERS!",
 "I KNOW EVERY|ITEM BY HEART.",
 "SPRING SEEDS ARE|IN! BUY ONE?", "THE SHOP SMELLS|OF FLOWERS NOW.",
 "HOT DAY. COLD|BERRY JUICE?", "THE DOOR STAYS|OPEN IN SUMMER.",
 "LEAVES ON THE|DOORSTEP AGAIN.", "PUMPKIN SOUP|SALE TODAY!",
 "BRRR! STAY BY|THE STOVE.", "WINTER STOCK IS|THIN. SORRY.",
 "RAIN KEEPS THE|SHOPPERS HOME.", "MY ROOF DRIPS.|AGAIN.",
 "YOU ARE A GOOD|CUSTOMER!", "I SAVED YOU THE|BEST PRICE.",
 "WE ARE FRIENDS|NOW, AREN'T WE?", "I'D TRUST YOU|WITH THE KEYS.",
 "YOU ARE FAMILY.|TAKE ANYTHING!", "THE SHOP IS HALF|YOURS ALREADY."],
"BRAM": [
 "THE BELL WAS ONCE|LOUD AS THUNDER.", "THIS BENCH HAS SEEN|FIFTY WINTERS.", "SLOW DOWN, HERO.|THE DAY IS LONG.",
 "A QUIET VILLAGE IS|A HAPPY ONE.", "I REMEMBER WHEN|THIS WAS A FIELD.", "TEA BEFORE WORDS.|ALWAYS.",
 "LISTEN TO THE WIND.|IT TALKS.", "ELDERWICK STANDS|BECAUSE WE HELP.", "MY KNEES FORECAST|THE WEATHER.",
 "THE BELL WILL RING|AGAIN. I FEEL IT.",
 "THE FIRST BUDS!|MY FAVOURITE DAY.", "SPRING BRINGS|NEW HOPE.",
 "THE SUN IS KIND TO|OLD BONES.", "I NAP IN THE SHADE|ALL SUMMER.",
 "THE LEAVES FALL.|THE YEAR RESTS.", "HARVEST SOON.|GOOD, GOOD.",
 "WINTER SLEEPS THE|WORLD. REST TOO.", "KEEP WARM, HERO.|ALWAYS.",
 "RAIN IS THE SKY'S|OLD LULLABY.", "MY BENCH IS WET.|I'LL STAND.",
 "YOU REMIND ME OF|ME, LONG AGO.", "GOOD HEROES LISTEN.|YOU DO.",
 "THE VILLAGE LIKES|YOU. SO DO I.", "SIT, FRIEND.|THERE'S ROOM.",
 "YOU'VE MADE THIS|OLD MAN PROUD.", "ELDERWICK IS YOUR|HOME FOR LIFE."],
"PIP": [
 "I CAUGHT A BUG!|IT ESCAPED.", "ARE YOU A REAL|KNIGHT?", "I CAN RUN FAST.|WATCH ME!",
 "MY MUM SAYS NO|SLIMES. I SAY YES.", "WHAT'S YOUR SWORD|CALLED?", "I'M GOING TO BE A|HERO TOO!",
 "THE CAT FOLLOWS ME|EVERYWHERE.", "I'M NOT TIRED!|...YAWN.", "SECRET: I LIKE|THE BELL.",
 "HAVE YOU SEEN A|FROG TODAY?",
 "FLOWERS!! SO MANY|FLOWERS!", "THE FROGS ARE BACK!|HI FROGS!",
 "I SWAM IN THE POND.|DON'T TELL MUM.", "SO HOT! ICE CREAM?|WE HAVE NONE.",
 "I JUMPED IN LEAVES.|SO CRUNCHY!", "PUMPKINS ARE|BIGGER THAN ME!",
 "SNOW! I MADE A|SNOWMAN!", "MY NOSE IS COLD.|IS YOURS?",
 "PUDDLES!! SPLASH!|SPLASH!", "RAIN MAKES THE|WORMS DANCE.",
 "YOU'RE COOL.|REALLY COOL.", "WANNA SEE MY|ROCK COLLECTION?",
 "YOU'RE MY BEST|FRIEND! ...ALMOST.", "I TOLD EVERYONE|ABOUT YOU!",
 "WHEN I GROW UP I'LL|BE LIKE YOU!", "YOU'RE MY BEST|FRIEND, FOREVER!"],
"ODDA": [
 "SOUP'S ON! COME|IN, COME IN.", "I POUR THE BEST|CIDER IN TOWN.", "THE STEW IS A|FAMILY SECRET.",
 "A FULL BELLY MAKES|A BRAVE HERO.", "THE FIRE'S ALWAYS|LIT HERE.", "BREAD IS BAKING.|SMELL THAT?",
 "BRAM NEVER PAYS HIS|TAB. I LET HIM.", "WIPE YOUR BOOTS!|THANK YOU.", "ASK ME ANYTHING.|I HEAR IT ALL.",
 "TAVERN RULE ONE:|BE KIND.",
 "SPRING GREENS IN|THE SOUP TODAY!", "NEW BARREL OF|SPRING ALE!",
 "COLD CIDER FOR A|HOT DAY!", "THE PATIO IS FULL|THIS SUMMER.",
 "PUMPKIN PIE IN THE|OVEN!", "MULLED CIDER TIME.|COSY!",
 "HOT STEW FOR COLD|FEET!", "THE HEARTH IS FULL.|COME CLOSER.",
 "RAINY DAYS FILL MY|TAVERN.", "DRY OFF BY THE FIRE|FIRST.",
 "YOUR FIRST BOWL IS|ALWAYS HALF PRICE.", "YOU EAT WELL.|I LIKE THAT.",
 "A SEAT IS ALWAYS|SAVED FOR YOU.", "TELL ME ABOUT THE|ROAD TODAY.",
 "YOU ARE THE HEART|OF THIS TAVERN.", "FREE SOUP FOR LIFE.|DON'T TELL BRAM."],
"FINN": [
 "THE FISH ARE SHY|TODAY.", "PATIENCE. THAT'S|THE WHOLE TRICK.", "I HAVEN'T CAUGHT|A THING. YET.",
 "THE POND KNOWS|MY NAME.", "SHH! YOU'LL SCARE|THEM.", "A BIG ONE GOT AWAY.|THIS BIG!",
 "MY BOAT NEEDS|PAINT. ALWAYS.", "THE SEA TALKS.|I LISTEN.", "WORMS: THE FISH|LOVE THEM.",
 "A GOOD LINE IS|WORTH GOLD.",
 "SPRING FISH ARE|LAZY AND FAT.", "THE RIVER IS HIGH.|GOOD FISHING!",
 "SUMMER SUN. FISH|GO DEEP.", "NIGHT FISHING IN|SUMMER. HEAVEN!",
 "AUTUMN RUN! THEY|JUMP INTO THE NET.", "THE WATER TURNS|COLD AND CLEAR.",
 "ICE ON THE POND.|NO FISHING.", "I MEND NETS IN|WINTER.",
 "FISH BITE IN THE|RAIN. GO FISH!", "WET, BUT HAPPY.|FISH ARE UP.",
 "YOU'RE LEARNING.|NOT BAD.", "TRY THE SHADY SPOT.|SECRET.",
 "I NEVER SHARE MY|SPOT. BUT YOU...", "WE'RE A GOOD TEAM,|YOU AND I.",
 "BEST FISHING MATE|I EVER HAD.", "THE SEA SENDS ITS|THANKS."],
"TESS": [
 "CLANG CLANG!|MUSIC TO MY EARS.", "EVERY NAIL HAS|ITS PLACE.", "MEASURE TWICE.|CUT ONCE.",
 "I BUILD, YOU WALK.|FAIR DEAL.", "NEED AN AXE? LET ME|SEE THAT BLADE.", "WOOD, STONE, ORE.|MY THREE LOVES.",
 "THE FORGE BITES.|WATCH YOUR HANDS.", "BRAM'S BENCH IS|MY FINEST WORK.", "A WELL-BUILT WALL|LASTS AGES.",
 "MORE WOOD! ALWAYS|MORE WOOD!",
 "NEW PLANKS FOR THE|SPRING REPAIRS.", "THE THAW CRACKS|EVERYTHING.",
 "HOT FORGE, HOT SUN.|I'M MELTING.", "BEST BUILDING DAYS|OF THE YEAR.",
 "FIX ROOFS BEFORE|THE FROST.", "THE FORGE FEELS|COZY NOW.",
 "THE FORGE IS THE|WARMEST SPOT.", "FROZEN NAILS. BAD|FOR BUSINESS.",
 "LEAKY ROOFS. SO|MUCH WORK.", "RAIN ON HOT IRON.|HISSSS!",
 "GOOD HANDS. YOU'D|MAKE A BUILDER.", "KEEP YOUR TOOLS|DRY. TRUST ME.",
 "YOU AND I, WE BUILD|THIS VILLAGE.", "I'LL FIX YOUR GEAR|FREE. DEAL?",
 "THE BEST THING I|EVER BUILT: YOU.", "TESS AND HERO,|BUILDERS FOR LIFE."],
"LUMA": [
 "HERBS HEAL.|FOREST SINGS.", "THE FOREST IS|ALWAYS LISTENING.", "MOSS REMEMBERS|EVERYTHING.",
 "BREW TEA, FIND|PEACE.", "DON'T STEP ON THE|MUSHROOMS.", "I TALK TO TREES.|THEY'RE KIND.",
 "EVERY LEAF HAS A|STORY.", "SMELL THAT?|THAT'S RAIN COMING.", "SLIMES ARE JUST|SAD PUDDLES.",
 "THE OLD OAK KNOWS|THE BEST SECRETS.",
 "BUDS! SPROUTS!|THE FOREST WAKES.", "SPRING HERBS ARE|THE STRONGEST.",
 "BERRIES RIPEN IN|THE SUMMER SUN.", "FIREFLIES DANCE|WITH THE FERNS.",
 "MUSHROOMS SPRING UP|AFTER AUTUMN RAIN.", "THE LEAVES ARE|GOLD AND RED.",
 "THE FOREST SLEEPS.|SPEAK SOFTLY.", "SNOW ON THE PINES.|PURE QUIET.",
 "THE FOREST DRINKS.|SO DO I.", "RAIN ON LEAVES IS|THE BEST SONG.",
 "THE TREES LIKE YOU.|THEY TOLD ME.", "TAKE A LEAF. KEEP|IT FOR LUCK.",
 "YOU WALK SOFTLY.|THE FOREST NOTICES.", "COME FOR TEA ANY|TIME, FRIEND.",
 "THE FOREST CALLS|YOU ITS OWN.", "I'VE MADE YOU A|CHARM. WEAR IT."],
"WREN": [
 "FAR ROADS, FINE|GOODS. LOOK!", "I'M HERE TWICE A|WEEK. NOT MORE.", "I'VE SEEN MOUNTAINS|MADE OF SALT.",
 "THIS COAT IS FROM|THE NORTH.", "MY WAGON HAS A|LIFE OF ITS OWN.", "TRADE IS A DANCE.|LEARN THE STEPS.",
 "STRANGE CHEESES IN|FAR TOWNS.", "ELDERWICK IS MY|FAVOURITE STOP.", "A PENNY SAVED IS A|PENNY EARNED.",
 "SHH. THE BEST DEALS|ARE WHISPERED.",
 "ROADS ARE MUDDY.|MY WHEELS SULK.", "SPRING SEEDS FROM|THE SOUTH!",
 "DUSTY ROADS.|THIRSTY HORSE.", "SUMMER TRADE IS|BRISK AND BRIGHT!",
 "HARVEST TRADE! MY|BEST SEASON.", "WINDY ROADS. HOLD|YOUR HAT.",
 "SNOWED IN TWICE|THIS MONTH!", "I SELL SCARVES IN|WINTER. OBVIOUSLY.",
 "WET WAGON. WET|GOODS. WET ME.", "RAIN BRINGS BUYERS|UNDER MY AWNING.",
 "FOR YOU? A SMALL|DISCOUNT.", "I SAVED THE GOOD|STUFF FOR YOU.",
 "YOU'RE MY BEST|CUSTOMER, FRIEND.", "I'LL WRITE YOU|FROM THE ROAD.",
 "I'D STAY ALL YEAR,|IF I COULD.", "ELDERWICK AND YOU:|MY TWO FAVOURITES."],
"MISO": [
 "MEOW.", "MRRP?", "PURR... PURR...", "(MISO STARES)", "(MISO BLINKS)", "MROW!",
 "(MISO WASHES A PAW)", "MEW.", "(TAIL TWITCH)", "HSSS... KIDDING.",
 "(CHASES BUTTERFLY)", "MEOW! (FLOWERS!)",
 "(NAPS IN THE SUN)", "(WARM FUR. HAPPY.)",
 "(PLAYS WITH LEAVES)", "MROW! (CRUNCH!)",
 "(CURLS UP BY FIRE)", "BRRR. MEW.",
 "(HATES THE RAIN)", "HSSS! WET PAWS!",
 "(RUBS YOUR LEG)", "PURRR.",
 "(HEAD BUMP!)", "(SITS NEAR YOU)",
 "(NAPS ON YOUR LAP)", "PURRRRRR..."],
}

ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .,!?'-:/+()%=")

ERRORS = []
def check(s, kind):
    try:
        check1(s, kind)
    except AssertionError as e:
        ERRORS.append(str(e))

def check1(s, kind):
    assert "??" not in s, s
    lines = s.split("|")
    maxlen = {"band": 14, "title": 20, "name": 8}.get(kind, 19)
    maxlines = 1 if kind in ("band", "title", "name") else 2
    assert len(lines) <= maxlines, (kind, s)
    for ln in lines:
        assert 0 < len(ln) <= maxlen, (kind, s, len(ln))
        for ch in ln:
            assert ch in ALLOWED, (kind, s, ch)

texts = []   # (kind, string)
def T(kind, s):
    check(s, kind)
    texts.append((kind, s))
    return len(texts) - 1

for n in NPC_NAMES: T("name", n)
title_ids = [T("title", q[0]) for q in QUESTS]
step_ids = []
steps_flat = []
first_step = []
for q in QUESTS:
    first_step.append(len(steps_flat))
    for st in q[5]:
        steps_flat.append(st)
        step_ids.append((T("line", st[4]), T("band", st[5])))
if ERRORS:
    print('\n'.join(ERRORS)); sys.exit(1)
assert max(title_ids) < 255 and max(i for p in step_ids for i in p) < 255
thanks0 = len(texts)
for s in THANKS: T("line", s)
reply0 = len(texts)
for s in REPLIES: T("line", s)
misc = {k: T("line", v) for k, v in MISC.items()}
amb0 = len(texts)
for n in NPC_NAMES[1:]:
    assert len(AMBIENT[n]) == 26, n
    for s in AMBIENT[n]: T("line", s)

if ERRORS:
    print('\n'.join(ERRORS)); sys.exit(1)
assert len(steps_flat) <= 80, len(steps_flat)

# ---------------------------------------------------------------- text_data.h
blob = "".join(s + "\0" for _, s in texts)
offs = []
o = 0
for _, s in texts:
    offs.append(o); o += len(s) + 1
assert o <= 12288, o
with open(OUT / "text_data.h", "w") as f:
    f.write("// GENERATED by tools/gen_quest_data.py. Do not edit; change the generator and rerun it.\n")
    f.write("// All strings: upper case, font glyphs only, '|' is the line break, 2 lines of at most 19 characters.\n")
    f.write("#pragma once\n#include \"platform.h\"\n\n")
    f.write("enum {\n  TX_NAME0 = 0,                 // 10 NPC names, indexed by GQ_NPC_*\n")
    f.write(f"  TX_THANKS0 = {thanks0}, TX_THANKS_N = {len(THANKS)},\n")
    f.write(f"  TX_REPLY0 = {reply0}, TX_REPLY_N = {len(REPLIES)},\n")
    f.write(f"  TX_OUCH = {misc['OUCH']}, TX_DING = {misc['DING']},\n")
    f.write(f"  TX_AMBIENT0 = {amb0},            // + (npc - 1) * 26 + k, npc 1..9; k: 0..9 general, 10..17 season*2+i,\n")
    f.write("                                // 18..19 rain, 20..25 friendship tier (1..3) * 2 + i\n")
    f.write("  TX_AMBIENT_PER_NPC = 26,\n")
    f.write(f"  TX_COUNT = {len(texts)}\n}};\n\n")
    f.write("static const uint16_t TEXT_OFF[TX_COUNT] GC_ROM_ATTR = {")
    for i, v in enumerate(offs):
        f.write(("\n  " if i % 16 == 0 else " ") + f"{v},")
    f.write("\n};\n\n")
    f.write("static const char TEXT_BLOB[] GC_ROM_ATTR =\n")
    for _, s in texts:
        f.write('  "' + s + '\\0"\n')
    f.write(";\n")
    f.write("static_assert(sizeof(TEXT_BLOB) <= 12288, \"text blob over budget (design 11.7)\");\n")
    f.write(f"static_assert(sizeof(TEXT_BLOB) == {o + 1}, \"blob and offsets out of sync\");\n")

# -------------------------------------------------------------- quest_data.h
with open(OUT / "quest_data.h", "w") as f:
    f.write("// GENERATED by tools/gen_quest_data.py. Do not edit; change the generator and rerun it.\n")
    f.write("// Quest tables (design 4.2). All tables are read with GC_RD8.\n")
    f.write("#pragma once\n#include \"platform.h\"\n\n")
    f.write("enum { QR_NONE, QR_DAY, QR_QUEST, QR_TOOL, QR_FRIEND, QR_PROJECT, QR_SEASON };\n")
    f.write("enum { QS_TALK, QS_FETCH, QS_DELIVER, QS_GOTO, QS_SLAY, QS_DO, QS_WAIT_DAY, QS_SCENE };\n")
    f.write("// Step arguments: TALK npc, give item (0 none) | FETCH item, count | DELIVER npc, item, count |\n")
    f.write("// GOTO area, x, y | SLAY monster, count (0 = grows with the year), area | DO activity, count, 1 = before noon |\n")
    f.write("// SCENE scene id, -, 1 = festival day only\n")
    f.write("typedef struct { uint8_t giver, first_step, n_steps, req_kind, req_val, reward_coins5, reward_item, title_txt; } QuestDef;\n")
    f.write("typedef struct { uint8_t type, a, b, c, txt_start, txt_band; } QuestStep;\n\n")
    f.write(f"#define QUEST_COUNT {len(QUESTS)}\n#define QSTEP_COUNT {len(steps_flat)}\n")
    f.write("enum { QUEST_STORY0 = 0, QUEST_SIDE0 = 6, QUEST_ERRAND0 = 20, QUEST_LEGEND0 = 28 };\n")
    f.write("enum { QP_BOARD = 0, QP_GARDEN, QP_BRIDGE, QP_WELL, QP_STALL, QP_LANTERNS, QP_FLOWERBEDS, QP_BELL, QP_STAGE, QP_FOUNTAIN };\n\n")
    f.write("static const QuestDef QUESTS[QUEST_COUNT] GC_ROM_ATTR = {\n")
    for i, q in enumerate(QUESTS):
        n = len(q[5])
        f.write(f"  {{{q[1]}, {first_step[i]}, {n}, {q[2][0]}, {q[2][1]}, {q[3]}, {q[4]}, {title_ids[i]}}},  // {i} {q[0]}\n")
    f.write("};\n\nstatic const QuestStep QSTEPS[QSTEP_COUNT] GC_ROM_ATTR = {\n")
    for i, st in enumerate(steps_flat):
        f.write(f"  {{{st[0]}, {st[1]}, {st[2]}, {st[3]}, {step_ids[i][0]}, {step_ids[i][1]}}},\n")
    f.write("};\n\n// Story chapters: earliest day, need (0 none, 1 tool (tool<<4)|level, 2 project), value.\n")
    f.write("static const uint8_t STORY_GATE[6][3] GC_ROM_ATTR = {\n")
    for g in STORY_GATE:
        f.write(f"  {{{g[0]}, {g[1]}, {g[2]}}},\n")
    f.write("};\n")

print(f"{len(QUESTS)} quests, {len(steps_flat)} steps, {len(texts)} strings, blob {o + 1} bytes")
