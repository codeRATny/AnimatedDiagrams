# AnimatedDiagrams — Multi-Agent Workspace roadmap

> Status: planning draft. Reconstructed from the agreed project discussion; implementation details and version milestones require validation against the repository.

## Goal

Enable multiple AI agents to collaborate on animated diagrams and presentations through MCP while the AnimatedDiagrams GUI is running. Long-term, an AI agent should be able to create, animate, and export a complete presentation without manual editing.

## MVP: Multi-Agent Workspace

### Agent lifecycle
- Add MCP tool `register_agent` returning a unique `agentId`.
- Require an explicit `agentId` in agent-scoped MCP calls; return a clear error for missing or invalid IDs.
- Track agent registration, connection/disconnection, document ownership or association, and cleanup.
- Target up to 10 concurrent agents for the MVP.

### GUI visualization
- Display per-agent cursors and activity in the Qt GUI using an `AgentOverlay`.
- Associate each agent with its current document and show which document or element it is editing.
- Keep visual state consistent when agents disconnect or change documents.

### Concurrent editing
- Serialize document-mutating operations to avoid conflicting edits.
- Define deterministic ordering, error reporting, and document revision handling.
- Ensure the GUI and MCP actions use the same document state.

### MCP runtime constraints
- MCP is available only while the main GUI application is running.
- A standalone daemon and `stdio` transport are out of scope for the MVP.

## Suggested implementation stages

1. **Agent identity:** implement registration, `agentId` validation, lifecycle and tests.
2. **Workspace state:** maintain active agents and their document associations.
3. **AgentOverlay:** render cursors and agent activity in the Qt UI.
4. **Edit coordination:** serialize mutations, handle stale state and failures.
5. **Multi-agent validation:** exercise concurrent workflows with up to 10 agents.

## MVP acceptance criteria

- Multiple agents can register and receive distinct identifiers.
- Agent-scoped calls without a valid `agentId` fail predictably.
- The GUI shows the active agent cursors and their document associations.
- Concurrent edits do not corrupt document state.
- Agent termination removes or marks its GUI state correctly.
- The MCP interface does not run independently of the GUI.

## Longer-term roadmap (directional)

The broader roadmap discussed for versions 2.2–4.4 includes:
- Scenario logic and branching.
- OpenTelemetry integration.
- Automatic layout and positioning.
- A unified document/data model.
- AI-driven creation of complete animated presentations, including animation, timing and export.

> Exact version-to-feature assignments are intentionally omitted here because the original detailed roadmap was not available for verification.

## Out of scope for this branch's initial planning document

- Automatic merging into `develop` or `main`.
- Building a standalone MCP daemon.
- Requiring an external orchestration service for the GUI MVP.

## Open design questions

- How are agents authenticated and how long does an `agentId` remain valid?
- Which operations are serialized per document versus globally?
- How should the GUI display conflicts, disconnections and agent activity?
- What is the desired API for assigning or switching documents?
