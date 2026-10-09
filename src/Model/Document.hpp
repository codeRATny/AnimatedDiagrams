#ifndef _MODEL_DOCUMENT_HPP_
#define _MODEL_DOCUMENT_HPP_

#include <cstddef>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "Model/Model.hpp"

/// @file Document.hpp
/// @brief Editable document: the model, editing operations and undo/redo history.

namespace ad
{

/// Generates ids "<prefix>_<7 base36 chars>" unique within a model.
class IdGenerator
{
public:
    IdGenerator();
    explicit IdGenerator(uint32_t seed);
    std::string Next(std::string_view prefix, const Model &model);

private:
    std::mt19937 _rng;
};

/// Scene duration derived from the steps: max(4 s, last step end + 1.2 s rounded up to 0.5 s);
/// a duration set by the user is never shrunk.
double AutoDuration(const Scenario &s);

class Document
{
public:
    static constexpr size_t kMaxHistory = 200;

    Document() = default;
    explicit Document(Model m) : _model(std::move(m)) {}

    [[nodiscard]] const Model &Get() const { return _model; }
    /// In-place access; call Checkpoint() before modifying.
    [[nodiscard]] Model &Mutable() { return _model; }

    /// Replace the whole model (new / opened document); clears the history.
    void Reset(Model m);

    /// Remember the current state in the history. Consecutive checkpoints with the same
    /// non-empty merge key are coalesced (typing a label = one undo step).
    void Checkpoint(std::string_view merge_key = {});
    /// Break coalescing (the next checkpoint creates a new history entry).
    void BreakMerge() { _last_merge_key.clear(); }

    [[nodiscard]] bool CanUndo() const { return !_undo.empty(); }
    [[nodiscard]] bool CanRedo() const { return !_redo.empty(); }
    bool               Undo();
    bool               Redo();

    // -----------------------------------------------------------------------
    // Operations (each one creates a history entry)
    // -----------------------------------------------------------------------
    Node &AddNode(Vec2 center, const ElementType &type);
    /// nullptr when from == to or a node is missing.
    Edge *AddEdge(std::string_view from, std::string_view to, std::string_view from_port = {}, std::string_view to_port = {});
    void  RemoveNode(std::string_view id);
    void  RemoveEdge(std::string_view id);
    void  RemoveStep(std::string_view id);

    Port *AddPort(std::string_view node_id, std::optional<Vec2> offset = std::nullopt);
    void  RemovePort(std::string_view node_id, std::string_view port_id);

    void AddWaypoint(std::string_view edge_id, Vec2 p, std::optional<size_t> index = std::nullopt);
    void RemoveWaypoint(std::string_view edge_id, size_t index);
    void ReverseEdge(std::string_view edge_id);

    Step &AddStep(Step s);
    Step *DuplicateStep(std::string_view id);
    /// Default step of the given type at time `at` (nullopt when there are no nodes / edges for it).
    [[nodiscard]] std::optional<Step> MakeDefaultStep(StepType type, double at) const;
    /// Fill missing fields after a step type change.
    void NormalizeStep(Step &s) const;

    void SetDuration(double ms);
    void SortSteps();
    void UpdateDuration();

    // -----------------------------------------------------------------------
    // Markers (chapters). Times are clamped to [0, duration]; two markers never share a time.
    // -----------------------------------------------------------------------
    /// New marker (id "m_..."); nullptr when there already is a marker at that time.
    Marker *AddMarker(double time, std::string label = {});
    /// Move a marker; false when it is missing or another marker is at the new time.
    /// Moves with the same non-empty merge key in a row are one undo step (dragging).
    bool MoveMarker(std::string_view id, double time, std::string_view merge_key = {});
    /// false when the marker is missing.
    bool RenameMarker(std::string_view id, std::string label);
    /// false when the marker is missing.
    bool RemoveMarker(std::string_view id);

    // -----------------------------------------------------------------------
    // Document library
    // -----------------------------------------------------------------------
    void UpsertElement(const ElementType &e);
    void UpsertEffect(const EffectDef &e);
    void UpsertAnimation(const AnimationTemplate &a);
    void UpsertDesignSystem(const DesignSystem &d);
    /// Remove a library item by id from any of the three lists; false if absent.
    bool RemoveLibraryItem(std::string_view id);

    std::string NewId(std::string_view prefix) { return _ids.Next(prefix, _model); }

private:
    Model              _model;
    std::vector<Model> _undo;
    std::vector<Model> _redo;
    std::string        _last_merge_key;
    IdGenerator        _ids;
};

} // namespace ad

#endif // _MODEL_DOCUMENT_HPP_
