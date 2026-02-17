# HelixScreen Architecture Diagrams

Visual architecture documentation for the HelixScreen codebase.

Each diagram exists in two formats:
- **Mermaid** (`.md`) — renders in GitHub, VS Code, and most markdown viewers
- **D2** (`.d2`) — renders to SVG/PNG via `d2` CLI for polished output

## Diagrams

| Diagram | What it shows |
|---------|---------------|
| [1. System Overview](./01-system-overview.md) | High-level subsystems and external connections |
| [2. Data Flow](./02-data-flow.md) | WebSocket → PrinterState → Subjects → XML → UI |
| [3. Threading Model](./03-threading-model.md) | Thread boundaries, UpdateQueue, safety rules |
| [4. UI Layer](./04-ui-layer.md) | Panels, overlays, modals, navigation stack |
| [5. Startup Sequence](./05-startup-sequence.md) | Application boot phases and init order |
| [6. Singleton Map](./06-singleton-map.md) | All singletons grouped by responsibility |

For detailed implementation patterns, conventions, and code examples, see the [Architecture Guide](../devel/ARCHITECTURE.md).

## Rendering D2 Diagrams

```bash
# Install d2: https://d2lang.com/tour/install
brew install d2

# Render all diagrams
for f in docs/architecture/*.d2; do
  d2 "$f" "${f%.d2}.svg"
done

# Or render one with live preview
d2 --watch docs/architecture/01-system-overview.d2
```
