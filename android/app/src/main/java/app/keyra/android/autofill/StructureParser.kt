package app.keyra.android.autofill

import android.app.assist.AssistStructure
import android.view.View
import android.view.autofill.AutofillId
import app.keyra.android.match.Target

/** The text fields of a screen, in traversal order, with what the form belongs to. */
class ParsedStructure(
    val fields: List<FieldInfo>,
    val ids: Map<Int, AutofillId>,
    val target: Target,
) {
    val form: LoginForm by lazy { FormClassifier.classify(fields) }

    fun idsOf(indices: List<Int>): List<AutofillId> = indices.mapNotNull(ids::get)

    /** The typed text of the first field among [indices] (only used on a save request). */
    fun textOf(indices: List<Int>): String = indices.firstNotNullOfOrNull { i -> fields.firstOrNull { it.index == i }?.text?.takeIf(String::isNotEmpty) } ?: ""
}

object StructureParser {
    fun parse(structure: AssistStructure, withValues: Boolean): ParsedStructure {
        val fields = mutableListOf<FieldInfo>()
        val ids = mutableMapOf<Int, AutofillId>()
        var webDomain: String? = null

        fun visit(node: AssistStructure.ViewNode) {
            if (webDomain == null && !node.webDomain.isNullOrEmpty()) webDomain = node.webDomain
            val id = node.autofillId
            if (id != null && node.autofillType == View.AUTOFILL_TYPE_TEXT && node.visibility == View.VISIBLE) {
                val html = node.htmlInfo
                val attrs = html?.attributes.orEmpty().associate { (it.first ?: "").lowercase() to (it.second ?: "") }
                if (html == null || html.tag.equals("input", ignoreCase = true)) {
                    val index = fields.size
                    ids[index] = id
                    fields += FieldInfo(
                        index = index,
                        hints = node.autofillHints?.toList().orEmpty(),
                        inputType = node.inputType,
                        htmlType = attrs["type"],
                        htmlAutocomplete = attrs["autocomplete"],
                        idEntry = node.idEntry ?: attrs["name"] ?: attrs["id"],
                        hint = node.hint?.toString(),
                        focused = node.isFocused,
                        text = if (withValues && node.autofillValue?.isText == true) node.autofillValue?.textValue?.toString() else null,
                    )
                }
            }
            for (i in 0 until node.childCount) visit(node.getChildAt(i))
        }

        for (w in 0 until structure.windowNodeCount) visit(structure.getWindowNodeAt(w).rootViewNode)
        return ParsedStructure(fields, ids, Target(webDomain, structure.activityComponent.packageName))
    }
}
