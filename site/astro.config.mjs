import { defineConfig } from "astro/config";
import { unified } from "@astrojs/markdown-remark";
import docLinks from "./remark-doc-links.mjs";

export default defineConfig({
    output: "static",
    markdown: { processor: unified({ remarkPlugins: [docLinks] }) },
});
