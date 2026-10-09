package app.keyra.android.autofill

import android.app.assist.AssistStructure
import android.view.View
import android.view.autofill.AutofillId
import app.keyra.android.match.Browsers
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
        val packageName = structure.activityComponent.packageName
        val browser = Browsers.isBrowser(packageName)
        // The page's domain is the first one met; fields of a frame from another domain
        // (an embedded ad or widget) are left out so nothing is offered or saved for them.
        var pageDomain: String? = null

        fun visit(node: AssistStructure.ViewNode, inherited: String?) {
            val domain = node.webDomain?.takeIf { it.isNotEmpty() } ?: inherited
            if (pageDomain == null && domain != null) pageDomain = domain
            val id = node.autofillId
            val samePage = !browser || domain == null || domain == pageDomain
            if (id != null && samePage && node.autofillType == View.AUTOFILL_TYPE_TEXT && node.visibility == View.VISIBLE) {
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
            for (i in 0 until node.childCount) visit(node.getChildAt(i), domain)
        }

        for (w in 0 until structure.windowNodeCount) visit(structure.getWindowNodeAt(w).rootViewNode, null)
        return ParsedStructure(fields, ids, Target(pageDomain.takeIf { browser }, packageName))
    }
}
