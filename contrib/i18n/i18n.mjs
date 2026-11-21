#!/usr/bin/env node
// Checks the PartyBot server text (src/game/PlayerBots/i18n/<lang>.json, format in
// src/game/PlayerBots/Shared/Phrases.h). Run from anywhere: node contrib/i18n/i18n.mjs [--all]
//   - both files are JSON objects of string | string[] | { tag: string | string[] }
//   - every key of a translation is in zh.json (the base language)
//   - a key's {variables} are the same in every language
//   - keys the code uses that zh.json lacks; keys nothing seems to use
//   - how much of each translation is done (--all lists the missing keys)
// Exit code 1 when there are errors (the first three), 0 otherwise.

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const dir = path.join(root, 'src/game/PlayerBots/i18n');
const BASE = 'zh';
const listAll = process.argv.includes('--all');
let errors = 0;
const error = msg => { errors++; console.log('ERROR ' + msg); };
const warn = msg => console.log('warn  ' + msg);

function lines(value) {
    if (typeof value === 'string') return [value];
    if (Array.isArray(value)) return value;
    return Object.values(value).flatMap(lines);
}
const varsOf = value => new Set(lines(value).flatMap(l => [...String(l).matchAll(/\{(\w+)\}/g)].map(m => m[1])));

function load(lang) {
    const file = path.join(dir, lang + '.json');
    let data;
    try { data = JSON.parse(fs.readFileSync(file, 'utf8')); }
    catch (e) { error(`${lang}.json: ${e.message}`); return {}; }
    const out = {};
    for (const [key, value] of Object.entries(data)) {
        if (key.startsWith('//')) continue;
        const ok = typeof value === 'string' || (Array.isArray(value) && value.every(v => typeof v === 'string')) ||
            (value && typeof value === 'object' && !Array.isArray(value) &&
             Object.values(value).every(v => typeof v === 'string' || (Array.isArray(v) && v.every(s => typeof s === 'string'))));
        if (!ok) { error(`${lang}.json "${key}": not a string, string[] or { tag: string | string[] }`); continue; }
        out[key] = value;
    }
    return out;
}

const langs = fs.readdirSync(dir).filter(f => f.endsWith('.json')).map(f => f.slice(0, -5));
const tables = Object.fromEntries(langs.map(l => [l, load(l)]));
const base = tables[BASE] || {};

// translations against the base
for (const lang of langs.filter(l => l !== BASE)) {
    const table = tables[lang];
    for (const key of Object.keys(table)) {
        if (!(key in base)) { error(`${lang}.json "${key}": not in ${BASE}.json`); continue; }
        const a = [...varsOf(base[key])].sort().join(','), b = [...varsOf(table[key])].sort().join(',');
        if (a !== b) error(`${lang}.json "${key}": variables {${b}} but ${BASE}.json has {${a}}`);
    }
}

// the code: dotted string literals of the key families zh.json has
const families = new Set(Object.keys(base).map(k => k.split('.')[0]));
const used = new Set(), prefixes = new Set();
function walk(d) {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
        const p = path.join(d, e.name);
        if (e.isDirectory()) walk(p);
        else if (/\.(cpp|h)$/.test(e.name)) {
            const text = fs.readFileSync(p, 'utf8').replace(/\/\/.*$/gm, '').replace(/\/\*[\s\S]*?\*\//g, '');
            for (const m of text.matchAll(/"([a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+)(\.?)"/g)) {
                if (!families.has(m[1].split('.')[0])) continue;
                if (m[2]) prefixes.add(m[1] + '.');         // "affinity.trait." + key
                else used.add(m[1]);
            }
            for (const m of text.matchAll(/"([a-z][a-z0-9_]*\.)"/g))
                if (families.has(m[1].slice(0, -1))) prefixes.add(m[1]);
        }
    }
}
walk(path.join(root, 'src/game'));
const isPrefix = key => [...prefixes].some(p => key.startsWith(p));
for (const key of [...used].sort())
    if (!(key in base) && !Object.keys(base).some(k => k.startsWith(key + '.')))
        warn(`code uses "${key}", ${BASE}.json has no such key`);
const unused = Object.keys(base).filter(k => !used.has(k) && !isPrefix(k));
if (unused.length) warn(`${unused.length} keys of ${BASE}.json not found in the code (built from a prefix, or left over): ${unused.join(', ')}`);

// coverage
const total = Object.keys(base).length;
for (const lang of langs.filter(l => l !== BASE)) {
    const missing = Object.keys(base).filter(k => !(k in tables[lang]));
    console.log(`${lang}: ${total - missing.length} / ${total} keys translated`);
    if (listAll && missing.length) console.log('  missing: ' + missing.join(', '));
}
console.log(errors ? `${errors} error(s)` : 'ok');
process.exit(errors ? 1 : 0);
