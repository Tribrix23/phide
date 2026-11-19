/**
 * @file ErdAndMigrationEngine.hpp
 * @brief Deterministic SQL DDL parser, Schema Diff engine, Migration SQL generator, and
 *        multi-notation ERD SVG exporter (#10).
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "phide/db/SqlEngine.hpp"

namespace phide::db {

/**
 * @enum ErdNotation
 * @brief Supported Entity-Relationship Diagram visual notations.
 */
enum class ErdNotation {
    CrowsFoot = 0, ///< Standard Crow's Foot (||, O|, |<, O<)
    Uml       = 1, ///< UML Class / Association (1, 0..1, 0..*, 1..*, +/#/- visibility)
    Chen      = 2, ///< Peter Chen (Entities, Diamond Relationships, Oval Attributes, 1/N)
    Barker    = 3  ///< Oracle Barker (Soft boxes, #/* /o markers, solid/dashed half-lines + crow's foot)
};

/**
 * @struct ErdNodeLayout
 * @brief Canvas coordinates for one entity table box in the ERD view.
 */
struct ErdNodeLayout {
    std::wstring tableName;
    float x = 0.0f;
    float y = 0.0f;
    float width = 220.0f;
    float height = 140.0f;
};

/**
 * @enum ErdEntityRole
 * @brief Semantic classification inferred by the Schema Interpretation Engine.
 */
enum class ErdEntityRole {
    Strong = 0,          ///< Core entity with primary key (parent or standalone domain table)
    Junction = 1,        ///< M:N associative bridge table (>=2 FKs covering key columns)
    WeakIdentifying = 2, ///< Weak / dependent entity whose composite PK includes an FK
    Standalone = 3       ///< Unconnected utility/migration/session/log table
};

/**
 * @struct ErdEntityInfo
 * @brief Interpreted semantic metadata and topological hierarchy layer for one table.
 */
struct ErdEntityInfo {
    size_t tableIdx = 0;
    ErdEntityRole role = ErdEntityRole::Strong;
    std::wstring badgeLabel; ///< Optional short semantic badge ("M:N", "WEAK")
    int layer = 0;
    size_t inboundFkCount = 0;
    size_t outboundFkCount = 0;
    bool isStandalone = false;
};

/**
 * @struct ErdInterpretedRelation
 * @brief Normalized parent->child relationship extracted from explicit FKs or optional *_id inference.
 */
struct ErdInterpretedRelation {
    size_t parentTableIdx = 0;
    size_t childTableIdx = 0;
    int parentColIdx = -1;
    int childColIdx = -1;
    std::wstring fkColumn;
    std::wstring refColumn;
    std::wstring relationLabel = L"has";
    bool childOptional = true;
    bool isOneToOne = false;
    bool isIdentifying = false;
    bool isSelfRef = false;
    bool isInferred = false;
};

/**
 * @struct ErdSchemaInterpretation
 * @brief Full semantic interpretation of tables, roles, layers, and relationships.
 */
struct ErdSchemaInterpretation {
    std::vector<ErdEntityInfo> entities;
    std::vector<ErdInterpretedRelation> relations;
};

/**
 * @struct ErdPointF
 * @brief Lightweight 2D point for routed orthogonal ERD connectors.
 */
struct ErdPointF {
    float x = 0.0f;
    float y = 0.0f;
};

/**
 * @struct ErdLineJump
 * @brief Orthogonal crossing bridge arc on segment `[points[segmentIndex], points[segmentIndex+1]]`.
 */
struct ErdLineJump {
    size_t segmentIndex = 0;
    float crossX = 0.0f; ///< X coordinate where an orthogonal vertical segment crosses this horizontal segment
};

/**
 * @struct ErdRoutedEdge
 * @brief Complete obstacle-free orthogonal path, separated port anchors, notation metadata, and line jumps.
 */
struct ErdRoutedEdge {
    size_t relationIndex = 0;
    size_t parentTableIdx = 0;
    size_t childTableIdx = 0;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float dir1 = 1.0f; ///< +1 exits right edge, -1 exits left edge
    float x2 = 0.0f;
    float y2 = 0.0f;
    float dir2 = -1.0f;
    bool childOptional = true;
    bool isOneToOne = false;
    bool isIdentifying = false;
    bool isSelfRef = false;
    bool isInferred = false;
    std::wstring relationLabel = L"has";
    std::vector<ErdPointF> points;
    std::vector<ErdLineJump> jumps;
    ErdPointF midPoint{};
};

/**
 * @enum SchemaDiffKind
 * @brief Granular structural difference between a baseline/target schema and a desired/source schema.
 */
enum class SchemaDiffKind {
    TableAdded = 0,
    TableRemoved,
    ColumnAdded,
    ColumnRemoved,
    ColumnModified,
    ForeignKeyAdded,
    ForeignKeyRemoved
};

/**
 * @struct SchemaDiffItem
 * @brief One atomic schema delta produced by ComputeSchemaDiff.
 */
struct SchemaDiffItem {
    SchemaDiffKind kind = SchemaDiffKind::TableAdded;
    std::wstring tableName;
    std::wstring itemName;   ///< Column name or FK column name (empty for TableAdded/Removed)
    std::wstring oldSpec;    ///< Previous type/constraint summary
    std::wstring newSpec;    ///< Desired type/constraint summary
    std::wstring summary;    ///< Human-readable single-line diff summary
};

/**
 * @struct SchemaDiffResult
 * @brief Complete diff summary and executable SQL migration script between two schemas.
 */
struct SchemaDiffResult {
    std::wstring baselineLabel;
    std::wstring targetLabel;
    std::vector<SchemaDiffItem> items;
    std::wstring migrationSql;
    size_t addedCount = 0;
    size_t modifiedCount = 0;
    size_t removedCount = 0;
};

/**
 * @class ErdAndMigrationEngine
 * @brief Pure stateless SQL DDL parser, schema differ, and SQL migration generator.
 */
class ErdAndMigrationEngine {
public:
    /**
     * @brief Parses SQL `CREATE TABLE (...)` statements from a `.sql` file or DDL string into SqlTableInfo.
     */
    [[nodiscard]] static std::vector<SqlTableInfo> ParseSqlDdlSchema(const std::wstring& ddlSql);

    /**
     * @brief Computes deterministic differences needed to migrate `baseline` -> `desired`.
     */
    [[nodiscard]] static SchemaDiffResult ComputeSchemaDiff(
        const std::vector<SqlTableInfo>& baseline,
        const std::vector<SqlTableInfo>& desired,
        SqlDialect dialect = SqlDialect::MySQL,
        const std::wstring& baselineLabel = L"Baseline",
        const std::wstring& desiredLabel = L"Target");

    /**
     * @brief Generates full `CREATE TABLE` DDL SQL for all tables in `schema`.
     */
    [[nodiscard]] static std::wstring GenerateFullDdl(
        const std::vector<SqlTableInfo>& schema,
        SqlDialect dialect = SqlDialect::MySQL);

    /**
     * @brief Interprets table roles (Strong / M:N Junction / Weak / Standalone), DAG layers,
     *        and explicit (+ optional *_id inferred) FK relationships.
     */
    [[nodiscard]] static ErdSchemaInterpretation InterpretSchema(
        const std::vector<SqlTableInfo>& tables,
        bool inferImplicitFks = false);

    /**
     * @brief Computes a layered topological + barycenter-ordered node layout that aligns
     *        parent->child hierarchies and separates standalone tables to prevent tangled wires.
     */
    [[nodiscard]] static std::vector<ErdNodeLayout> ComputeSmartLayout(
        const std::vector<SqlTableInfo>& tables,
        const std::vector<ErdNodeLayout>& sizedNodes,
        ErdNotation notation,
        bool inferImplicitFks = false);

    /**
     * @brief Routes every interpreted relationship along non-overlapping orthogonal tracks with
     *        column-aligned spread ports, obstacle avoidance around table boxes, and line jumps.
     */
    [[nodiscard]] static std::vector<ErdRoutedEdge> RouteErdEdges(
        const std::vector<SqlTableInfo>& tables,
        const std::vector<ErdNodeLayout>& nodes,
        const ErdSchemaInterpretation& interp,
        ErdNotation notation);

    /**
     * @brief Exports the ERD layout (in the selected notation) as a self-contained UTF-8 SVG document.
     * @param showManyToManyBadges When false (default), hides M:N junction badges / <<junction>> stereotypes.
     */
    [[nodiscard]] static std::string ExportErdToSvg(
        const std::vector<SqlTableInfo>& tables,
        const std::vector<ErdNodeLayout>& nodes,
        ErdNotation notation,
        bool inferImplicitFks = false,
        bool showManyToManyBadges = false);
};

} // namespace phide::db
