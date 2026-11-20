import { defineCollection } from "astro:content";
import { glob } from "astro/loaders";

const reference = defineCollection({
    loader: glob({
        base: "../docs",
        pattern: "**/*.md",
        generateId: ({ entry }) =>
            entry
                .replace(/\\/g, "/")
                .replace(/(?:^|\/)README\.md$/, "")
                .replace(/\.md$/, ""),
    }),
});

export const collections = { reference };
