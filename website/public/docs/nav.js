// The documentation search: every searchable section (a title, its page and
// its anchor) filtered on the client. The page navigation itself is static
// HTML from templates/partials.

const SECTIONS = [
    { t: "Getting started: download the SDK", p: "getting-started.html", a: "download" },
    { t: "Getting started: the tools", p: "getting-started.html", a: "tools" },
    { t: "Getting started: your first asset", p: "getting-started.html", a: "first-asset" },
    { t: "Getting started: seeds, variants and archetypes", p: "getting-started.html", a: "seeds" },
    { t: "Getting started: the editor", p: "getting-started.html", a: "editor" },
    ...LANGUAGE_SECTIONS,
    { t: "Format: versions", p: "format.html", a: "versions" },
    { t: "Format: the file layout", p: "format.html", a: "layout" },
    { t: "Format: the sections", p: "format.html", a: "sections" },
    { t: "Format: the virtual machine", p: "format.html", a: "vm" },
    { t: "Format: determinism", p: "format.html", a: "determinism" },
    { t: "Backends: the CLI toolchain", p: "backends.html", a: "cli" },
    { t: "Backends: HTML5 / WASM", p: "backends.html", a: "html5" },
    { t: "Backends: the C API", p: "backends.html", a: "capi" },
    { t: "Backends: SFML and Godot", p: "backends.html", a: "engines" },
    { t: "Backends: VS Code", p: "backends.html", a: "vscode" },
    { t: "FAQ: determinism", p: "faq.html", a: "determinism" },
    { t: "FAQ: seed, variant, archetype", p: "faq.html", a: "seed" },
    { t: "FAQ: why a VM", p: "faq.html", a: "vm" },
    { t: "FAQ: composed assets", p: "faq.html", a: "parts" },
    { t: "FAQ: outputs", p: "faq.html", a: "outputs" },
    { t: "FAQ: contributing", p: "faq.html", a: "contribute" },
];

function bindSearch() {
    const search = document.getElementById("search");
    const results = document.getElementById("results");
    if (!search || !results) return;

    const close = () => { results.style.display = "none"; };
    search.addEventListener("input", () => {
        const query = search.value.trim().toLowerCase();
        results.textContent = "";
        if (query.length < 2) { close(); return; }
        const matches = SECTIONS.filter((s) => s.t.toLowerCase().includes(query)).slice(0, 8);
        for (const section of matches) {
            const link = document.createElement("a");
            link.href = section.p + "#" + section.a;
            link.textContent = section.t;
            results.appendChild(link);
        }
        results.style.display = matches.length ? "block" : "none";
    });
    search.addEventListener("keydown", (event) => {
        if (event.key === "Escape") { search.value = ""; close(); search.blur(); }
        if (event.key === "Enter" && results.firstChild) results.firstChild.click();
    });
    document.addEventListener("keydown", (event) => {
        const typing = event.target.closest && event.target.closest("input, textarea, select");
        if (event.key === "/" && !typing) { event.preventDefault(); search.focus(); }
    });
    document.addEventListener("click", (event) => {
        if (!event.target.closest("#search-wrap")) close();
    });
}

document.addEventListener("DOMContentLoaded", bindSearch);
