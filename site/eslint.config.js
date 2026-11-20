import js from "@eslint/js";
import astro from "eslint-plugin-astro";
import globals from "globals";
import tseslint from "typescript-eslint";

export default [
    { ignores: ["dist/**", ".astro/**"] },
    { ...js.configs.recommended, files: ["**/*.{js,mjs}"] },
    ...tseslint.configs.recommended.map((config) => ({
        ...config,
        files: ["**/*.ts"],
    })),
    ...astro.configs.recommended,
    {
        files: ["**/*.{js,mjs}"],
        languageOptions: { globals: { ...globals.browser, ...globals.node } },
    },
];
