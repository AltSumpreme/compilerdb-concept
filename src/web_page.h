#pragma once

// The embedded web page (single header, vanilla JS, no build step).
// Cytoscape.js from CDN. Final-review interactive version:
//   * initial view = object nodes + direct neighbours (?kind=object),
//     "Load full graph" button fetches /api/graph
//   * click a node -> fetch /api/neighborhood?path=... and merge it in
//   * per-kind layer toggles (source/header/object/flag)
//   * "Circular includes" panel fed by /api/cycles, with highlight

namespace cgdb {

inline const char* kWebPage = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>CompileGraphDB</title>
<script src="https://unpkg.com/cytoscape@3/dist/cytoscape.min.js"></script>
<style>
  body { margin: 0; font-family: sans-serif; }
  #hdr { padding: 6px 12px; background: #222; color: #eee; font-size: 13px;
         display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
  #hdr button { background: #444; color: #eee; border: 1px solid #666;
                border-radius: 3px; padding: 2px 8px; cursor: pointer; }
  #hdr button.off { opacity: 0.45; text-decoration: line-through; }
  #main { position: absolute; top: 37px; bottom: 0; left: 0; right: 0;
          display: flex; }
  #cy { flex: 1; }
  #panel { width: 260px; overflow-y: auto; border-left: 1px solid #ccc;
           padding: 8px; font-size: 12px; background: #fafafa; }
  #panel h2 { font-size: 13px; margin: 4px 0; }
  #panel .group { margin-bottom: 8px; }
  #panel .path { cursor: pointer; color: #2456a6; word-break: break-all; }
  #panel .path:hover { text-decoration: underline; }
  #status { color: #aaa; font-size: 11px; }
</style>
</head>
<body>
<div id="hdr">
  <b>CompileGraphDB</b>
  <button id="load-full">Load full graph</button>
  layers:
  <button class="layer" data-kind="source">source</button>
  <button class="layer" data-kind="header">header</button>
  <button class="layer" data-kind="object">object</button>
  <button class="layer" data-kind="flag">flag</button>
  <button id="hl-cycles">Highlight cycles</button>
  <span id="status"></span>
</div>
<div id="main">
  <div id="cy"></div>
  <div id="panel"><h2>Circular includes</h2><div id="cycles">loading…</div></div>
</div>
<script>
const KIND_COLORS = { source: '#4C8BF5', header: '#F5A623',
                      object: '#7ED321', flag: '#BD10E0' };
const layerVisible = { source: true, header: true, object: true, flag: true };
const knownNodes = new Set(), knownEdges = new Set();

const cy = cytoscape({
  container: document.getElementById('cy'),
  elements: [],
  style: [
    { selector: 'node', style: {
        'label': 'data(label)', 'font-size': 7,
        'text-valign': 'bottom', 'text-margin-y': 2,
        'background-color': ele => KIND_COLORS[ele.data('kind')] || '#999',
        'width': 10, 'height': 10 } },
    { selector: 'edge', style: {
        'width': 1, 'line-color': '#bbb',
        'target-arrow-shape': 'triangle', 'target-arrow-color': '#bbb',
        'arrow-scale': 0.6, 'curve-style': 'bezier' } },
    { selector: '.cycle', style: {
        'background-color': '#e00', 'border-width': 2, 'border-color': '#900' } },
    { selector: 'edge.cycle', style: {
        'line-color': '#e00', 'target-arrow-color': '#e00', 'width': 2.5 } }
  ]
});

function status(msg) { document.getElementById('status').textContent = msg; }

// Merge a /api/graph-shaped document into the current graph.
function mergeGraph(doc, relayout) {
  const add = [];
  for (const n of doc.nodes) {
    const id = 'n' + n.id;
    if (knownNodes.has(id)) continue;
    knownNodes.add(id);
    add.push({ data: { id: id, label: n.label, kind: n.kind } });
  }
  for (const e of doc.edges) {
    const id = 'e' + e.from + '_' + e.to + '_' + e.kind;
    if (knownEdges.has(id)) continue;
    if (!knownNodes.has('n' + e.from) || !knownNodes.has('n' + e.to)) continue;
    knownEdges.add(id);
    add.push({ data: { id: id, source: 'n' + e.from, target: 'n' + e.to,
                       kind: e.kind } });
  }
  if (add.length) {
    cy.add(add);
    applyVisibility();
    if (relayout) cy.layout({ name: 'cose', animate: false }).run();
  }
  status(cy.nodes().length + ' nodes, ' + cy.edges().length + ' edges shown');
}

function loadInitial() {
  knownNodes.clear(); knownEdges.clear();
  cy.elements().remove();
  fetch('/api/graph?kind=object')
    .then(r => r.json())
    .then(g => mergeGraph(g, true));
}

document.getElementById('load-full').onclick = () => {
  fetch('/api/graph').then(r => r.json()).then(g => mergeGraph(g, true));
};

// Click-to-expand: pull the node's 1-hop neighborhood and merge it.
cy.on('tap', 'node', ev => {
  const label = ev.target.data('label');
  fetch('/api/neighborhood?path=' + encodeURIComponent(label))
    .then(r => r.ok ? r.json() : null)
    .then(g => { if (g) mergeGraph(g, true); });
});

// Layer toggles: hide/show a kind's nodes and all incident edges.
function applyVisibility() {
  cy.batch(() => {
    cy.nodes().forEach(n => {
      n.style('display', layerVisible[n.data('kind')] ? 'element' : 'none');
    });
    cy.edges().forEach(e => {
      const s = e.source().data('kind'), t = e.target().data('kind');
      e.style('display', layerVisible[s] && layerVisible[t] ? 'element' : 'none');
    });
  });
}
document.querySelectorAll('.layer').forEach(btn => {
  btn.onclick = () => {
    const k = btn.dataset.kind;
    layerVisible[k] = !layerVisible[k];
    btn.classList.toggle('off', !layerVisible[k]);
    applyVisibility();
  };
});

// Circular includes panel + highlight.
let cycleGroups = [];
function highlightCycles() {
  cy.elements().removeClass('cycle');
  const memberSet = new Set(cycleGroups.flat());
  cy.nodes().forEach(n => {
    if (memberSet.has(n.data('label'))) n.addClass('cycle');
  });
  cy.edges().forEach(e => {
    if (e.data('kind') === 'includes' &&
        memberSet.has(e.source().data('label')) &&
        memberSet.has(e.target().data('label'))) e.addClass('cycle');
  });
}
document.getElementById('hl-cycles').onclick = highlightCycles;

fetch('/api/cycles').then(r => r.json()).then(doc => {
  cycleGroups = doc.cycles;
  const box = document.getElementById('cycles');
  box.textContent = '';
  if (!doc.cycles.length) {
    box.textContent = 'no circular includes';
    return;
  }
  doc.cycles.forEach((group, i) => {
    const div = document.createElement('div');
    div.className = 'group';
    const title = document.createElement('b');
    title.textContent = 'group ' + (i + 1) + ' (' + group.length + ' members)';
    div.appendChild(title);
    group.forEach(p => {
      const line = document.createElement('div');
      line.className = 'path';
      line.textContent = p;
      line.onclick = () => {   // click a path: expand it in the graph
        fetch('/api/neighborhood?path=' + encodeURIComponent(p))
          .then(r => r.ok ? r.json() : null)
          .then(g => { if (g) { mergeGraph(g, true); highlightCycles(); } });
      };
      div.appendChild(line);
    });
    box.appendChild(div);
  });
});

loadInitial();
</script>
</body>
</html>
)HTML";

}  // namespace cgdb
