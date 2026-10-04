#include "AiCommandPolicy.h"
#include "mcp/McpCatalog.h"
#include "mcp/McpValidate.h"
#include "mcp/McpJson.h"
#include <QSet>

namespace {
bool containsFileAccess(const QJsonValue &v) {
    if (v.isArray()) { for (const auto &item : v.toArray()) if (containsFileAccess(item)) return true; }
    if (v.isObject()) {
        const auto o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) {
            const QString key = it.key().toLower();
            if (key == QLatin1String("paths") || key.endsWith(QLatin1String("path")) || key.endsWith(QLatin1String("uri"))
                || key.endsWith(QLatin1String("url")) || key.contains(QLatin1String("apikey"))
                || key == QLatin1String("authorization") || containsFileAccess(it.value())) return true;
        }
    }
    return false;
}
}
bool AiCommandPolicy::allowed(const QString &tool) {
    static const QSet<QString> tools = {
        QStringLiteral("catalog"), QStringLiteral("toolbox"), QStringLiteral("search"),
        QStringLiteral("inspect"), QStringLiteral("frames"), QStringLiteral("capture"), QStringLiteral("activity"), QStringLiteral("apply"),
        QStringLiteral("list_assets"), QStringLiteral("add_track"), QStringLiteral("set_track"),
        QStringLiteral("place_clip"), QStringLiteral("move_clip"), QStringLiteral("move_to_track"),
        QStringLiteral("set_trim"), QStringLiteral("set_duration"), QStringLiteral("split_clip"),
        QStringLiteral("delete_clip"), QStringLiteral("duplicate_clip"), QStringLiteral("set_overlap"),
        QStringLiteral("set_project_setup"), QStringLiteral("set_transform"), QStringLiteral("reset_transform"),
        QStringLiteral("add_text"), QStringLiteral("set_text"), QStringLiteral("set_subtitle_cues"),
        QStringLiteral("list_text_presets"), QStringLiteral("apply_text_preset"),
        QStringLiteral("list_text_animations"), QStringLiteral("set_text_animation"),
        QStringLiteral("list_effects"), QStringLiteral("add_effect"), QStringLiteral("set_effect_param"), QStringLiteral("remove_effect"),
        QStringLiteral("list_transitions"), QStringLiteral("add_transition"), QStringLiteral("remove_transition"),
        QStringLiteral("list_audio_effects"), QStringLiteral("add_audio_effect"), QStringLiteral("set_audio_effect_param"),
        QStringLiteral("set_keyframe"), QStringLiteral("list_keyframes"), QStringLiteral("set_keyframe_interpolation"),
        QStringLiteral("set_clip_speed"), QStringLiteral("set_speed_curve"), QStringLiteral("clear_speed_curve"), QStringLiteral("set_fade"), QStringLiteral("set_volume"),
        QStringLiteral("set_work_area"), QStringLiteral("clear_work_area"), QStringLiteral("seek"),
        QStringLiteral("generate_subtitles"), QStringLiteral("list_whisper_languages"),
        QStringLiteral("transcribe"), QStringLiteral("get_transcript"), QStringLiteral("get_job"),
        QStringLiteral("get_waveform"), QStringLiteral("assemble"), QStringLiteral("keep_ranges"), QStringLiteral("cut_words"),
        QStringLiteral("ai_capabilities"), QStringLiteral("detect_beats"), QStringLiteral("list_beats"),
    };
    return tools.contains(tool) && (drift::mcp::isKnownOp(tool) || drift::mcp::isHomepageTool(tool));
}
bool AiCommandPolicy::mutates(const QString &tool) {
    if (tool == QLatin1String("apply")) return true;
    if (drift::mcp::isHomepageTool(tool)) return false;
    return !drift::mcp::isReadOnlyOp(tool);
}
QJsonObject AiCommandPolicy::schema(const QString &tool) {
    if (!allowed(tool)) return {};
    for (const auto &v : drift::mcp::homepageTools()) {
        const auto o = v.toObject();
        if (o.value(QStringLiteral("name")).toString() == tool) return o.value(QStringLiteral("inputSchema")).toObject();
    }
    return drift::mcp::opInputSchema(tool);
}
QJsonObject AiCommandPolicy::validate(const QString &tool, QJsonObject &args) {
    using namespace drift::mcp;
    if (!allowed(tool)) return err("forbidden_op", QStringLiteral("Operation is not available to the in-app AI editor."));
    if (containsFileAccess(args)) return err("forbidden_args", QStringLiteral("AI cannot access paths, URLs or credentials."));
    QStringList ignored;
    const auto error = validateArgs(tool, schema(tool), args, &ignored);
    if (!error.isEmpty()) return error;
    if (!ignored.isEmpty()) return err("unknown_args", ignored.join(QLatin1Char(',')));
    if (tool == QLatin1String("apply")) {
        auto plan = args.value(QStringLiteral("ops")).toArray();
        for (const auto &v : plan) {
            const QString op = v.toObject().value(QStringLiteral("tool")).toString();
            if (isHomepageTool(op) || op == QLatin1String("generate_subtitles") || op == QLatin1String("transcribe"))
                return err("bad_args", QStringLiteral("Homepage tools and asynchronous caption jobs must be called separately, outside apply."));
        }
        const auto result = validatePlan(plan);
        if (!result.value(QStringLiteral("ok")).toBool()) return result;
        args.insert(QStringLiteral("ops"), plan);
    }
    if (tool == QLatin1String("capture") && args.value(QStringLiteral("full")).toBool()) return err("forbidden_args", QStringLiteral("Use inline preview only."));
    if (tool == QLatin1String("frames")) {
        if (args.value(QStringLiteral("return")).toString() == QLatin1String("path")) return err("forbidden_args", QStringLiteral("Use inline preview only."));
        args.insert(QStringLiteral("n"), qBound(1, args.value(QStringLiteral("n")).toInt(8), 12));
        args.insert(QStringLiteral("tile"), qBound(120, args.value(QStringLiteral("tile")).toInt(240), 360));
    }
    if (tool == QLatin1String("activity")) args.insert(QStringLiteral("samples"), qBound(8, args.value(QStringLiteral("samples")).toInt(80), 160));
    if (tool == QLatin1String("transcribe")) {
        if (args.value(QStringLiteral("engine")).toString(QStringLiteral("local")) != QLatin1String("local"))
            return err("forbidden_args", QStringLiteral("Only local Whisper is available to the AI editor."));
        args.insert(QStringLiteral("engine"), QStringLiteral("local"));
    }
    return ok();
}
QJsonObject AiCommandPolicy::validatePlan(QJsonArray &plan) {
    using namespace drift::mcp;
    if (plan.isEmpty() || plan.size() > 64) return err("bad_plan", QStringLiteral("Plan must contain 1..64 commands."));
    for (int i = 0; i < plan.size(); ++i) {
        if (!plan[i].isObject()) return err("bad_plan", QStringLiteral("Each command must be an object."));
        auto command = plan[i].toObject();
        if (command.size() > 2 || !command.value(QStringLiteral("tool")).isString()
            || (command.contains(QStringLiteral("args")) && !command.value(QStringLiteral("args")).isObject()))
            return err("bad_plan", QStringLiteral("Expected {tool:string,args:object}."));
        auto args = command.value(QStringLiteral("args")).toObject();
        const auto valid = validate(command.value(QStringLiteral("tool")).toString(), args);
        if (!valid.value(QStringLiteral("ok")).toBool()) return valid;
        command.insert(QStringLiteral("args"), args); plan[i] = command;
    }
    return ok();
}
QJsonValue AiCommandPolicy::redact(const QJsonValue &v) {
    if (v.isObject()) {
        QJsonObject out; const auto in = v.toObject();
        for (auto it = in.begin(); it != in.end(); ++it) {
            const QString key = it.key().toLower();
            if (key == QLatin1String("paths") || key.endsWith(QLatin1String("path")) || key.endsWith(QLatin1String("uri")) || key.endsWith(QLatin1String("url"))
                || key == QLatin1String("key") || key == QLatin1String("apikey") || key == QLatin1String("api_key") || key == QLatin1String("authorization")) continue;
            out.insert(it.key(), redact(it.value()));
        }
        return out;
    }
    if (v.isArray()) { QJsonArray out; for (const auto &x : v.toArray()) out.append(redact(x)); return out; }
    // Error prose can carry private paths. Replace any prose containing absolute path syntax.
    if (v.isString()) {
        const QString s = v.toString();
        if (s.contains(QLatin1String("/storage/")) || s.contains(QLatin1String("/home/"))
            || s.contains(QLatin1String("/data/")) || s.contains(QLatin1String("/tmp/")) || s.contains(QLatin1String("/Users/"))
            || s.contains(QLatin1String("content://")) || s.contains(QLatin1String("file://")) || s.contains(QLatin1String(":\\"))) return QStringLiteral("[private path omitted]");
    }
    return v;
}
