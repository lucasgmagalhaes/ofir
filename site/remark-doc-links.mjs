import { existsSync, statSync } from "node:fs";
import { dirname, relative, resolve, sep } from "node:path";
import { fileURLToPath } from "node:url";

const docsRoot = resolve(fileURLToPath(new URL("../docs/", import.meta.url)));
const repoRoot = resolve(docsRoot, "..");
const github = "https://github.com/lucasgmagalhaes/ofir";

function inside(root, target) {
  const path = relative(root, target);
  return path !== ".." && !path.startsWith(`..${sep}`) && !path.startsWith(sep);
}

function referencePath(path) {
  return path.replace(/\\/g, "/").replace(/(?:^|\/)README\.md$/, "").replace(/\.md$/, "");
}

export default function docLinks() {
  return (tree, file) => {
    if (!file.path || !inside(docsRoot, resolve(file.path))) return;

    function visit(node) {
      if ((node.type === "link" || node.type === "definition" || node.type === "image") &&
          node.url && !/^(?:[a-z][a-z\d+.-]*:|\/|#)/i.test(node.url)) {
        const match = /^([^?#]*)([?#].*)?$/.exec(node.url);
        const target = resolve(dirname(file.path), decodeURI(match[1]));
        const suffix = match[2] ?? "";
        if (inside(docsRoot, target) && target.endsWith(".md")) {
          node.url = `/reference/${referencePath(relative(docsRoot, target))}/${suffix}`;
        } else if (inside(repoRoot, target)) {
          const path = relative(repoRoot, target).replace(/\\/g, "/");
          const kind = existsSync(target) && statSync(target).isDirectory() ? "tree" : "blob";
          node.url = `${github}/${kind}/main/${path}${suffix}`;
        }
      }
      for (const child of node.children ?? []) visit(child);
    }

    visit(tree);
  };
}
