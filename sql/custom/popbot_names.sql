-- World population bots: hand-picked names for custom_popbot_name (table in popbot.sql).
-- Import into the WORLD database (mangos) after popbot.sql. Safe to run again (INSERT IGNORE).
-- race 0 = any race (names players pick), otherwise 1 Human, 2 Orc, 3 Dwarf, 4 Night Elf,
-- 5 Undead, 6 Tauren, 7 Gnome, 8 Troll. Letters only, max 12 characters, no lore characters.
-- The server reads the table at start (Identity::Load) and takes a table name half of the time.

-- Human
INSERT IGNORE INTO `custom_popbot_name` (`name`, `race`) VALUES
('Aldric',1),('Alaric',1),('Amelia',1),('Ansel',1),('Arden',1),('Arlen',1),('Ashford',1),('Barnaby',1),
('Bastian',1),('Beatrix',1),('Bedric',1),('Benedict',1),('Bertram',1),('Brenna',1),('Bryce',1),('Caelan',1),
('Calder',1),('Callum',1),('Carys',1),('Cedric',1),('Celia',1),('Clara',1),('Colette',1),('Corwin',1),
('Dalton',1),('Darian',1),('Deirdre',1),('Desmond',1),('Dorian',1),('Edgar',1),('Edith',1),('Edmund',1),
('Eleanor',1),('Elise',1),('Elric',1),('Emeric',1),('Evelyn',1),('Fiona',1),('Florian',1),('Gareth',1),
('Garrick',1),('Godric',1),('Gwendolyn',1),('Halden',1),('Harold',1),('Helena',1),('Hollis',1),('Isolde',1),
('Ivor',1),('Jasper',1),('Jocelyn',1),('Kendrick',1),('Lambert',1),('Leofric',1),('Linnea',1),('Lucian',1),
('Mabel',1),('Magnus',1),('Marian',1),('Matilda',1),('Merrick',1),('Mirabel',1),('Morgan',1),('Nolan',1),
('Odette',1),('Osric',1),('Percival',1),('Quentin',1),('Reginald',1),('Roderick',1),('Rowena',1),('Sabine',1),
('Seraphina',1),('Sybil',1),('Tamsin',1),('Theobald',1),('Tristan',1),('Wendell',1),('Wilhelm',1),('Winifred',1),
('Wystan',1),('Yvaine',1),('Lenora',1),('Rosalind',1),('Briony',1),('Cecily',1),('Elowen',1),('Hamish',1),
('Ingram',1),('Leopold',1),('Maeve',1),('Oswin',1),('Perrin',1),('Ravenna',1),('Sterling',1),('Thaddeus',1),
('Ysolde',1),('Brannock',1),('Corbin',1),('Delia',1),('Eldon',1),('Fenwick',1),('Galen',1),('Harlow',1),
('Imogen',1),('Kestrel',1),('Lyra',1),('Nessa',1),('Orrin',1),('Piers',1),('Rhiannon',1),('Soren',1),
('Tobias',1),('Verity',1),('Warrick',1),('Adelaide',1),('Ambrose',1),('Bennet',1),('Clement',1),('Dunstan',1),
('Estelle',1),('Felix',1),('Giles',1),('Hugo',1),('Josephine',1),('Lionel',1),('Margery',1),('Nathaniel',1),
('Oliver',1),('Philippa',1),('Rupert',1),('Stellan',1),('Tilda',1),('Wallace',1);

-- Orc
INSERT IGNORE INTO `custom_popbot_name` (`name`, `race`) VALUES
('Agrosh',2),('Bakgar',2),('Bolgrak',2),('Borguk',2),('Brakash',2),('Bruuk',2),('Dargash',2),('Dorgok',2),
('Draksha',2),('Drogan',2),('Duruk',2),('Gargash',2),('Garnak',2),('Ghorza',2),('Gorrak',2),('Grashna',2),
('Grimtak',2),('Grokk',2),('Gurtak',2),('Hagra',2),('Haknor',2),('Jorgash',2),('Karnog',2),('Kazgul',2),
('Korgash',2),('Kragga',2),('Krosh',2),('Magra',2),('Morgash',2),('Mukra',2),('Nagrok',2),('Narga',2),
('Ogrash',2),('Rakash',2),('Rogga',2),('Rugash',2),('Sharga',2),('Shazra',2),('Skorn',2),('Snagra',2),
('Tagrok',2),('Tharga',2),('Throk',2),('Torgash',2),('Ugrak',2),('Ulgra',2),('Urzak',2),('Vargash',2),
('Vorgul',2),('Zagrak',2),('Zharga',2),('Zorgun',2),('Barga',2),('Darok',2),('Drogga',2),('Garra',2),
('Gorza',2),('Hrokk',2),('Kargra',2),('Lokgar',2),('Mograth',2),('Narzug',2),('Ruzga',2),('Shagra',2),
('Thokk',2),('Urgosh',2),('Yazga',2),('Zugra',2),('Brogan',2),('Durza',2),('Grakka',2),('Mazgra',2),
('Orgak',2),('Raggok',2),('Sorza',2),('Tazgar',2),('Grukka',2),('Arkrosh',2),('Bargul',2),('Dakra',2),
('Gashnak',2),('Hurga',2),('Karza',2),('Lurgash',2),('Mugrok',2),('Nokra',2),('Ragash',2),('Strokk',2),
('Torka',2),('Uzgra',2),('Vrakk',2),('Zarnok',2),('Brukka',2),('Dragnar',2),('Golgra',2),('Krazga',2);

-- Dwarf
INSERT IGNORE INTO `custom_popbot_name` (`name`, `race`) VALUES
('Bardin',3),('Borin',3),('Bromdur',3),('Brunhild',3),('Dagna',3),('Brodin',3),('Brundar',3),('Dagmar',3),
('Dolgrim',3),('Dorna',3),('Dorrik',3),('Dulgar',3),('Durgan',3),('Durnir',3),('Edda',3),('Egrim',3),
('Falgrim',3),('Farna',3),('Gamlin',3),('Garrim',3),('Gerda',3),('Gildur',3),('Gorbin',3),('Grimbek',3),
('Grimna',3),('Gromli',3),('Grundi',3),('Gudrun',3),('Gurdal',3),('Hakon',3),('Haldor',3),('Hamlek',3),
('Helga',3),('Hilde',3),('Hrolf',3),('Hurgan',3),('Ingra',3),('Jorunn',3),('Kaldur',3),('Karga',3),
('Kazrik',3),('Keldra',3),('Kharim',3),('Kilda',3),('Korgan',3),('Kragni',3),('Lodrin',3),('Lorga',3),
('Magda',3),('Magnir',3),('Mordrek',3),('Morla',3),('Nalgrim',3),('Norri',3),('Odrik',3),('Olgra',3),
('Orvar',3),('Ragna',3),('Rekka',3),('Rogni',3),('Rurik',3),('Sigrun',3),('Skaldi',3),('Snorri',3),
('Sturla',3),('Thalgrim',3),('Thoren',3),('Thorvi',3),('Torga',3),('Torvald',3),('Tunna',3),('Ulfgar',3),
('Ulrika',3),('Vigdis',3),('Yrsa',3),('Brokkar',3),('Gimbrek',3),('Hjalmar',3),('Barrund',3),('Bergit',3),
('Duldrek',3),('Frela',3),('Grenda',3),('Haldis',3),('Kolbrun',3),('Morgrim',3),('Orsa',3),('Runa',3),
('Svala',3),('Thyra',3),('Valdrek',3),('Yngvar',3),('Baldrek',3),('Dunna',3),('Hammar',3),('Kettil',3);

-- Night Elf
INSERT IGNORE INTO `custom_popbot_name` (`name`, `race`) VALUES
('Aelwyn',4),('Alanthe',4),('Alyndra',4),('Amarith',4),('Anaya',4),('Arandis',4),('Arethil',4),('Belanor',4),
('Caelith',4),('Calendris',4),('Cyrilla',4),('Darnae',4),('Delenthia',4),('Elarion',4),('Elendra',4),('Elysia',4),
('Erendil',4),('Faelis',4),('Faerin',4),('Feyandra',4),('Galathil',4),('Ilarra',4),('Illendris',4),('Ilyana',4),
('Jaelis',4),('Kaelyn',4),('Keira',4),('Lathiel',4),('Lirael',4),('Lunara',4),('Lyandra',4),('Maelis',4),
('Melarith',4),('Miradel',4),('Myrael',4),('Naelith',4),('Nalendra',4),('Nerieth',4),('Nyssa',4),('Ophira',4),
('Quelaris',4),('Raelith',4),('Saelinn',4),('Selendra',4),('Seralyn',4),('Shandria',4),('Shaylee',4),('Sylvara',4),
('Talandris',4),('Thalendra',4),('Thaessa',4),('Tyrandel',4),('Valandor',4),('Velarion',4),('Vylara',4),('Ysandre',4),
('Zaelith',4),('Aeranis',4),('Coralyn',4),('Daelis',4),('Elarae',4),('Iriana',4),('Kaldoran',4),('Maeleth',4),
('Oriel',4),('Rhaelis',4),('Sarendis',4),('Tarindel',4),('Valaris',4),('Wyndell',4),('Aeris',4),('Ilaria',4),
('Lorandis',4),('Shaelis',4),('Tiriel',4),('Myrrine',4),('Anariel',4),('Elandor',4),('Feranis',4),('Hadriel',4),
('Larissa',4),('Nerendil',4),('Solanis',4),('Kalindra',4),('Merith',4),('Aelira',4),('Dalindra',4),('Eliara',4),
('Ithandris',4),('Lyssara',4),('Mythrel',4),('Naeris',4),('Sylandris',4),('Thalia',4),('Velinde',4),('Ynara',4);

-- Undead
INSERT IGNORE INTO `custom_popbot_name` (`name`, `race`) VALUES
('Ashgrave',5),('Belcain',5),('Corvin',5),('Crowley',5),('Dredmor',5),('Drevan',5),('Elias',5),('Erasmus',5),
('Ezekiel',5),('Gaunt',5),('Grimsby',5),('Hester',5),('Ichabod',5),('Ignatius',5),('Isadora',5),('Jareth',5),
('Lazarus',5),('Lenore',5),('Malachai',5),('Marrow',5),('Mortimer',5),('Morwen',5),('Nettle',5),('Obadiah',5),
('Ophelia',5),('Orlok',5),('Pallor',5),('Rotgut',5),('Sallow',5),('Septimus',5),('Silvanus',5),('Thorne',5),
('Ulrich',5),('Vesper',5),('Vincent',5),('Wraithe',5),('Wormwood',5),('Agatha',5),('Bram',5),('Cassius',5),
('Delphine',5),('Edwyn',5),('Fester',5),('Josiah',5),('Kasimir',5),('Lucan',5),('Mordecai',5),('Nyx',5),
('Osgood',5),('Prudence',5),('Quill',5),('Sorrow',5),('Tobiah',5),('Ursula',5),('Vale',5),('Wyatt',5),
('Zachariah',5),('Blight',5),('Cinder',5),('Dirge',5),('Eldritch',5),('Grimwald',5),('Hemlock',5),('Lament',5),
('Mourne',5),('Nocturne',5),('Pyre',5),('Requiem',5),('Rue',5),('Shroud',5),('Tallow',5),('Umbra',5),
('Vigil',5),('Withers',5),('Ashby',5),('Bellamy',5),('Cromwell',5),('Dorcas',5),('Ebenezer',5),('Felicity',5),
('Gilbert',5),('Horatio',5),('Jebediah',5),('Lucius',5),('Mercy',5),('Neville',5),('Percy',5),('Rosamund',5),
('Silas',5),('Theodric',5),('Verna',5),('Abner',5),('Cornelius',5),('Grimsley',5),('Mortis',5),('Thaddius',5);

-- Tauren
INSERT IGNORE INTO `custom_popbot_name` (`name`, `race`) VALUES
('Adahy',6),('Ahiga',6),('Aponi',6),('Ayasha',6),('Chayton',6),('Chenoa',6),('Elan',6),('Enapay',6),
('Halona',6),('Hania',6),('Honovi',6),('Huyana',6),('Istas',6),('Kachina',6),('Kanuna',6),('Kele',6),
('Kohana',6),('Kotori',6),('Lomasi',6),('Mahala',6),('Makya',6),('Mansi',6),('Matoskah',6),('Mika',6),
('Nayeli',6),('Nita',6),('Nodin',6),('Ohanzee',6),('Onawa',6),('Paytah',6),('Sakari',6),('Sani',6),
('Shilah',6),('Sihu',6),('Tadi',6),('Taima',6),('Takoda',6),('Tala',6),('Tiva',6),('Tokala',6),
('Tuari',6),('Wapi',6),('Wichapi',6),('Winona',6),('Yahto',6),('Yuma',6),('Ahoka',6),('Kodohide',6),
('Mulgar',6),('Taruk',6),('Tohana',6),('Unaga',6),('Hurak',6),('Morak',6),('Tahnoka',6),('Rahota',6),
('Kunah',6),('Mahka',6),('Oruna',6),('Sahne',6),('Turok',6),('Huanu',6),('Kahala',6),('Nohak',6),
('Tanuk',6),('Bahota',6),('Haloka',6),('Mokaro',6),('Ruhan',6),('Shona',6),('Wahota',6),('Yanaba',6),
('Ahote',6),('Hakan',6),('Kanosh',6),('Mato',6),('Oshana',6),('Pahana',6),('Tohopka',6),('Wanikiya',6),
('Stonehoof',6),('Mistmane',6),('Earthhoof',6),('Plainsrun',6),('Dawnhorn',6),('Oakmane',6),('Grasshoof',6),('Ridgehorn',6),
('Ashowa',6),('Kowan',6),('Mahone',6),('Tahomi',6),('Unoka',6),('Hotah',6),('Nakota',6),('Wakiya',6);

-- Gnome
INSERT IGNORE INTO `custom_popbot_name` (`name`, `race`) VALUES
('Bimble',7),('Binkle',7),('Bixie',7),('Bobbin',7),('Boffin',7),('Clicket',7),('Cogsby',7),('Dabble',7),
('Dinkum',7),('Dizzle',7),('Doodle',7),('Fenbix',7),('Fibbit',7),('Fidget',7),('Fizzwick',7),('Flinkle',7),
('Frizzle',7),('Gadget',7),('Gearlo',7),('Gibbet',7),('Giggles',7),('Gizmo',7),('Glimble',7),('Kettle',7),
('Klink',7),('Lappet',7),('Lizbet',7),('Mekkel',7),('Millie',7),('Minnow',7),('Nibbin',7),('Nixie',7),
('Nobble',7),('Noodle',7),('Nutley',7),('Pemble',7),('Pepper',7),('Pindle',7),('Pipwick',7),('Pixie',7),
('Plinko',7),('Poppet',7),('Quibble',7),('Ratchet',7),('Rivet',7),('Snibbet',7),('Spindle',7),('Sprocket',7),
('Squiggle',7),('Tibbit',7),('Tickle',7),('Tinsel',7),('Tizzy',7),('Toggle',7),('Trinket',7),('Twinkle',7),
('Twizzle',7),('Whistle',7),('Widget',7),('Wobble',7),('Zibbit',7),('Zinnia',7),('Zipple',7),('Zizzle',7),
('Bolty',7),('Fizzbang',7),('Gimblet',7),('Kizzy',7),('Nimbus',7),('Penny',7),('Sprinkle',7),('Thimble',7),
('Wizzle',7),('Dottie',7),('Fennick',7),('Gilly',7),('Hobble',7),('Jinx',7),('Klaxon',7),('Lolly',7),
('Mabbit',7),('Nackle',7),('Pazzle',7),('Rindle',7),('Sputter',7),('Tolly',7),('Wibble',7),('Bizzby',7),
('Cogwell',7),('Dimble',7),('Fuzzby',7),('Gearbit',7),('Mizzle',7),('Pocket',7),('Sparky',7),('Tumble',7);

-- Troll
INSERT IGNORE INTO `custom_popbot_name` (`name`, `race`) VALUES
('Batu',8),('Daka',8),('Gazjin',8),('Jaka',8),('Jalani',8),('Jazra',8),('Juzu',8),('Kazja',8),
('Keza',8),('Kojo',8),('Kuzan',8),('Lazji',8),('Mabo',8),('Mazal',8),('Mojo',8),('Nazjin',8),
('Nekali',8),('Rakaz',8),('Rezan',8),('Ruka',8),('Sazri',8),('Shanjo',8),('Sulja',8),('Tazri',8),
('Tikka',8),('Tujin',8),('Ulza',8),('Vanjin',8),('Vazri',8),('Vojin',8),('Wazu',8),('Xalji',8),
('Yenza',8),('Zabra',8),('Zaja',8),('Zanji',8),('Zekka',8),('Zimbo',8),('Zirak',8),('Zojin',8),
('Zulja',8),('Zuri',8),('Bazaka',8),('Dazjin',8),('Gozza',8),('Hekka',8),('Jinzo',8),('Kalji',8),
('Makazi',8),('Nazu',8),('Rokzi',8),('Sanza',8),('Tajin',8),('Unkaz',8),('Vazjin',8),('Zandu',8),
('Zekhan',8),('Zinja',8),('Bwonji',8),('Ganzi',8),('Hazuk',8),('Jengo',8),('Lokaji',8),('Mojin',8),
('Tekla',8),('Uzji',8),('Venji',8),('Wazira',8),('Yazul',8),('Jabari',8),('Kwazi',8),('Dakari',8),
('Nkozi',8),('Rezzi',8),('Tazjin',8),('Zaliki',8),('Bajin',8),('Gurzi',8),('Hazji',8),('Jozan',8),
('Kirazi',8),('Mazuka',8),('Rajin',8),('Shazi',8),('Tuzka',8),('Vozji',8),('Zakuu',8),('Zenjo',8);

-- Any race: names players pick
INSERT IGNORE INTO `custom_popbot_name` (`name`, `race`) VALUES
('Arrowsong',0),('Grimreaper',0),('Deathwish',0),('Nightfall',0),('Dawnbreaker',0),('Starfall',0),('Hexxed',0),('Critz',0),
('Backstabz',0),('Healz',0),('Tankard',0),('Mooncow',0),('Lockdown',0),('Shamwow',0),('Dotsalot',0),('Frostbyte',0),
('Holyhammer',0),('Stabbity',0),('Sneakypete',0),('Manaburn',0),('Spellbound',0),('Runeweaver',0),('Darkwhisper',0),('Emberfall',0),
('Ironclad',0),('Steelheart',0),('Stormcaller',0),('Silentstep',0),('Swiftarrow',0),('Ebonhawk',0),('Gloomweaver',0),('Hollowpoint',0),
('Bonecrusher',0),('Lightfoot',0),('Brightwing',0),('Cloudrunner',0),('Mistwalker',0),('Pinecone',0),('Valoria',0),('Mavren',0),
('Draven',0),('Zephyr',0),('Lyric',0),('Xandra',0),('Nyxie',0),('Raze',0),('Kaine',0),('Azrael',0),
('Seraph',0),('Jinxed',0),('Hexblade',0),('Felheart',0),('Truthseeker',0),('Wolfsbane',0),('Nightshade',0),('Bloodthorn',0),
('Doombringer',0),('Soulreaver',0),('Spellfire',0),('Arcanos',0),('Mystic',0),('Oracle',0),('Bubbles',0),('Sunshine',0),
('Pewpew',0),('Facemelter',0),('Oneshot',0),('Bigdeeps',0),('Healbot',0),('Tankyou',0),('Rofl',0),('Lolwut',0),
('Moonbeam',0),('Wildfire',0),('Ravenwood',0),('Blackthorn',0),('Duskwind',0),('Shadowmeld',0),('Frostfire',0),('Stormborn',0),
('Thornwood',0),('Wintermoon',0),('Graveborn',0),('Hellfire',0),('Lightning',0),('Riptide',0),('Sandstorm',0),('Tempest',0),
('Vortex',0),('Whisper',0),('Zealot',0),('Crusader',0),('Templar',0),('Warden',0),('Ranger',0),('Nomad',0),
('Wanderer',0),('Drifter',0),('Outlaw',0),('Rogueone',0),('Venom',0),('Viper',0),('Cobra',0),('Scorpion',0),
('Phoenix',0),('Griffin',0),('Wyvern',0),('Drake',0),('Hydra',0),('Kraken',0),('Basilisk',0),('Chimera',0),
('Gargoyle',0),('Banshee',0),('Spectre',0),('Revenant',0),('Valkyrie',0),('Paladinz',0),('Priestess',0),('Warlockz',0),
('Hunterx',0),('Magey',0),('Druidy',0),('Shammy',0),('Warri',0),('Lockie',0),('Stabby',0),('Pally',0);
