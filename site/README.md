# Ofir website

A static Astro site with a landing page, getting-started guide, and reference.

Use Node.js 22.23.4 or newer. From this directory, run `npm ci` and
`npm run dev` for local development.
Run `npm run build` to generate static pages in `dist/`, then
`npm run preview` to inspect the build. Astro imports the logo from
`docs/assets/logo.png` in the repository; no copy of the source asset is
needed.

Run `npm run lint` to check JavaScript, TypeScript, and Astro files. Run
`npm run format` to format the site and `npm run format:check` to verify it.

The reference loads Markdown directly from `../docs/` during the build. Each
document is published at `/reference/<path-without-.md>/`; a `README.md` is
published at its parent path. Relative links to other Markdown documents become
site links, while links to repository files outside `docs/` point to GitHub.
Edit the source files in `docs/`; no copy or export step is needed.
