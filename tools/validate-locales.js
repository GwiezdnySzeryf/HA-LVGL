'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const localesDirectory = path.join(root, 'www', 'locales');
const sourceLocale = 'pl';

function flatten(value, prefix = '', output = {}) {
    for (const [key, child] of Object.entries(value)) {
        const fullKey = prefix ? prefix + '.' + key : key;
        if (child && typeof child === 'object' && !Array.isArray(child)) flatten(child, fullKey, output);
        else output[fullKey] = child;
    }
    return output;
}

function placeholders(value) {
    return [...String(value).matchAll(/\{([a-zA-Z0-9_]+)\}/g)].map(match => match[1]).sort();
}

const localeFiles = fs.readdirSync(localesDirectory)
    .filter(file => /^[a-z]{2}(?:-[A-Z]{2})?\.json$/.test(file))
    .sort();

if (!localeFiles.includes(sourceLocale + '.json')) {
    throw new Error('Missing source locale: ' + sourceLocale + '.json');
}

const locales = Object.fromEntries(localeFiles.map(file => {
    const locale = file.replace(/\.json$/, '');
    const contents = JSON.parse(fs.readFileSync(path.join(localesDirectory, file), 'utf8'));
    return [locale, flatten(contents)];
}));

const source = locales[sourceLocale];
const sourceKeys = Object.keys(source).sort();
const errors = [];

for (const [locale, translations] of Object.entries(locales)) {
    const keys = Object.keys(translations).sort();
    for (const key of sourceKeys.filter(key => !keys.includes(key))) errors.push(locale + ': missing key ' + key);
    for (const key of keys.filter(key => !sourceKeys.includes(key))) errors.push(locale + ': unexpected key ' + key);
    for (const key of sourceKeys.filter(key => keys.includes(key))) {
        const expected = placeholders(source[key]);
        const actual = placeholders(translations[key]);
        if (expected.join(',') !== actual.join(',')) {
            errors.push(locale + ': placeholder mismatch in ' + key + ' (expected: ' + expected.join(', ') + ')');
        }
    }
}

const html = fs.readFileSync(path.join(root, 'www', 'index.html'), 'utf8');
const referencedKeys = [...html.matchAll(/data-i18n(?:-placeholder|-aria-label)?="([^"]+)"/g)]
    .map(match => match[1]);

for (const key of new Set(referencedKeys)) {
    if (!Object.prototype.hasOwnProperty.call(source, key)) errors.push('HTML: unknown translation key ' + key);
}

if (errors.length) {
    console.error(errors.join('\n'));
    process.exit(1);
}

console.log('Locales valid: ' + localeFiles.join(', ') + ' (' + sourceKeys.length + ' keys)');
