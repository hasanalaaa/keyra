package app.keyra.android.autofill

import android.text.InputType
import app.keyra.android.api.What

/** A view of one input field, independent of the Android structure classes (testable). */
data class FieldInfo(
    val index: Int,
    val hints: List<String> = emptyList(),
    val inputType: Int = 0,
    val htmlType: String? = null,
    val htmlAutocomplete: String? = null,
    val idEntry: String? = null,
    val hint: String? = null,
    val focused: Boolean = false,
    val text: String? = null,
)

data class LoginForm(val usernames: List<Int>, val passwords: List<Int>, val focused: Int?) {
    val empty: Boolean get() = usernames.isEmpty() && passwords.isEmpty()

    /** What to arm for the focused field: the password alone, or username (+ Tab + password). */
    fun what(): What = when {
        focused != null && focused in passwords -> What.PASSWORD
        focused != null && focused in usernames -> if (passwords.isEmpty()) What.USERNAME else What.BOTH
        usernames.isNotEmpty() && passwords.isNotEmpty() -> What.BOTH
        passwords.isNotEmpty() -> What.PASSWORD
        else -> What.USERNAME
    }
}

/** Finds username and password fields from hints, HTML attributes, input types and names. */
object FormClassifier {
    private val PASSWORD_HINTS = setOf("password", "current-password", "new-password", "newpassword")
    private val USERNAME_HINTS = setOf("username", "emailaddress", "email", "newusername", "phone", "tel")
    private val PASSWORD_WORDS = listOf("pass", "pwd", "كلمة", "مرور")
    private val USERNAME_WORDS = listOf("user", "login", "email", "e-mail", "mail", "account", "مستخدم", "بريد")
    private val SKIP_WORDS = listOf("search", "captcha", "otp", "one-time", "code", "بحث")

    private enum class Kind { USERNAME, PASSWORD, OTHER }

    fun classify(fields: List<FieldInfo>): LoginForm {
        val kinds = fields.associate { it.index to kindOf(it) }
        val passwords = fields.filter { kinds[it.index] == Kind.PASSWORD }.map { it.index }
        val usernames = fields.filter { kinds[it.index] == Kind.USERNAME }.map { it.index }.toMutableList()
        if (usernames.isEmpty() && passwords.isNotEmpty()) {
            // The text field just before the first password is the username on most forms.
            val first = fields.indexOfFirst { it.index == passwords.first() }
            fields.take(first).lastOrNull { isPlainText(it) && !skip(it) }?.let { usernames += it.index }
        }
        val focused = fields.firstOrNull { it.focused }?.index
        return LoginForm(usernames, passwords, focused)
    }

    private fun kindOf(f: FieldInfo): Kind {
        val hints = (f.hints + listOfNotNull(f.htmlAutocomplete)).flatMap { it.lowercase().split(' ') }
        if (hints.any { it in PASSWORD_HINTS }) return Kind.PASSWORD
        if (hints.any { it in USERNAME_HINTS }) return Kind.USERNAME
        if (f.htmlType?.lowercase() == "password" || isPasswordInput(f.inputType)) return Kind.PASSWORD
        if (f.htmlType?.lowercase() == "email" || isEmailInput(f.inputType)) return Kind.USERNAME
        if (!isPlainText(f) || skip(f)) return Kind.OTHER
        val words = listOfNotNull(f.idEntry, f.hint).joinToString(" ").lowercase()
        if (PASSWORD_WORDS.any { it in words }) return Kind.PASSWORD
        if (USERNAME_WORDS.any { it in words }) return Kind.USERNAME
        return Kind.OTHER
    }

    private fun skip(f: FieldInfo): Boolean {
        val words = listOfNotNull(f.idEntry, f.hint, f.htmlAutocomplete).joinToString(" ").lowercase()
        return SKIP_WORDS.any { it in words }
    }

    private fun isPlainText(f: FieldInfo): Boolean {
        val type = f.htmlType?.lowercase()
        if (type != null) return type == "text" || type == "email" || type == "tel"
        return f.inputType and InputType.TYPE_MASK_CLASS == InputType.TYPE_CLASS_TEXT
    }

    private fun isPasswordInput(t: Int): Boolean {
        val variation = t and InputType.TYPE_MASK_VARIATION
        return when (t and InputType.TYPE_MASK_CLASS) {
            InputType.TYPE_CLASS_TEXT -> variation == InputType.TYPE_TEXT_VARIATION_PASSWORD ||
                variation == InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD ||
                variation == InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD
            InputType.TYPE_CLASS_NUMBER -> variation == InputType.TYPE_NUMBER_VARIATION_PASSWORD
            else -> false
        }
    }

    private fun isEmailInput(t: Int): Boolean {
        val variation = t and InputType.TYPE_MASK_VARIATION
        return t and InputType.TYPE_MASK_CLASS == InputType.TYPE_CLASS_TEXT &&
            (variation == InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS || variation == InputType.TYPE_TEXT_VARIATION_WEB_EMAIL_ADDRESS)
    }
}
