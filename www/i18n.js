(function () {
    'use strict';

    const supportedLanguages = ['pl', 'en'];
    const fallbackLanguage = 'pl';
    const storageKey = 'tpp01.language';
    const dictionaries = {};
    let language = fallbackLanguage;

    function normalizeLanguage(value) {
        const normalized = String(value || '').toLowerCase().split('-')[0];
        return supportedLanguages.includes(normalized) ? normalized : fallbackLanguage;
    }

    function readPath(source, path) {
        return path.split('.').reduce((value, key) => value && value[key], source);
    }

    function interpolate(value, parameters) {
        return String(value).replace(/\{([a-zA-Z0-9_]+)\}/g, function (match, key) {
            return Object.prototype.hasOwnProperty.call(parameters, key) ? parameters[key] : match;
        });
    }

    async function loadDictionary(locale) {
        if (dictionaries[locale]) return dictionaries[locale];
        const response = await fetch('locales/' + locale + '.json', { cache: 'no-cache' });
        if (!response.ok) throw new Error('Unable to load locale: ' + locale);
        dictionaries[locale] = await response.json();
        return dictionaries[locale];
    }

    function translate(key, parameters) {
        const selected = readPath(dictionaries[language], key);
        const fallback = readPath(dictionaries[fallbackLanguage], key);
        return interpolate(selected === undefined ? (fallback === undefined ? key : fallback) : selected, parameters || {});
    }

    function applyTranslations(root) {
        const scope = root || document;
        scope.querySelectorAll('[data-i18n]').forEach(function (element) {
            element.textContent = translate(element.dataset.i18n);
        });
        scope.querySelectorAll('[data-i18n-placeholder]').forEach(function (element) {
            element.placeholder = translate(element.dataset.i18nPlaceholder);
        });
        scope.querySelectorAll('[data-i18n-aria-label]').forEach(function (element) {
            element.setAttribute('aria-label', translate(element.dataset.i18nAriaLabel));
        });
        document.title = translate('meta.title');
        document.documentElement.lang = language;
        document.querySelectorAll('[data-language]').forEach(function (button) {
            const active = button.dataset.language === language;
            button.classList.toggle('active', active);
            button.setAttribute('aria-pressed', active ? 'true' : 'false');
        });
    }

    async function setLanguage(locale) {
        const nextLanguage = normalizeLanguage(locale);
        await loadDictionary(fallbackLanguage);
        if (nextLanguage !== fallbackLanguage) await loadDictionary(nextLanguage);
        language = nextLanguage;
        localStorage.setItem(storageKey, language);
        applyTranslations();
        document.dispatchEvent(new CustomEvent('languagechange', { detail: { language: language } }));
    }

    async function init() {
        const saved = localStorage.getItem(storageKey);
        const preferred = saved || navigator.language || fallbackLanguage;
        try {
            await setLanguage(preferred);
        } catch (error) {
            console.error(error);
            language = fallbackLanguage;
        }
    }

    window.I18n = {
        apply: applyTranslations,
        getLanguage: function () { return language; },
        init: init,
        setLanguage: setLanguage,
        t: translate
    };
}());
