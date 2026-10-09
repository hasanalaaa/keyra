package app.keyra.android

import android.text.InputType
import app.keyra.android.api.What
import app.keyra.android.autofill.FieldInfo
import app.keyra.android.autofill.FormClassifier
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FormClassifierTest {
    private val text = InputType.TYPE_CLASS_TEXT
    private val password = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
    private val email = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS

    @Test
    fun nativeLoginWithHints() {
        val form = FormClassifier.classify(
            listOf(
                FieldInfo(0, hints = listOf("username"), inputType = text, focused = true),
                FieldInfo(1, hints = listOf("password"), inputType = password),
            ),
        )
        assertEquals(listOf(0), form.usernames)
        assertEquals(listOf(1), form.passwords)
        assertEquals(What.BOTH, form.what())
    }

    @Test
    fun focusedPasswordArmsPasswordOnly() {
        val form = FormClassifier.classify(
            listOf(
                FieldInfo(0, inputType = email),
                FieldInfo(1, inputType = password, focused = true),
            ),
        )
        assertEquals(listOf(0), form.usernames)
        assertEquals(What.PASSWORD, form.what())
    }

    @Test
    fun webFormByHtmlAttributes() {
        val form = FormClassifier.classify(
            listOf(
                FieldInfo(0, htmlType = "search", idEntry = "q"),
                FieldInfo(1, htmlType = "text", htmlAutocomplete = "username"),
                FieldInfo(2, htmlType = "password", htmlAutocomplete = "current-password"),
            ),
        )
        assertEquals(listOf(1), form.usernames)
        assertEquals(listOf(2), form.passwords)
    }

    @Test
    fun usernameIsTheTextFieldBeforeThePassword() {
        val form = FormClassifier.classify(
            listOf(
                FieldInfo(0, inputType = text, idEntry = "search_box"),
                FieldInfo(1, inputType = text, idEntry = "field1"),
                FieldInfo(2, inputType = password, idEntry = "field2"),
            ),
        )
        assertEquals(listOf(1), form.usernames)
        assertEquals(listOf(2), form.passwords)
    }

    @Test
    fun usernameStepOfATwoStepLogin() {
        val form = FormClassifier.classify(listOf(FieldInfo(0, inputType = text, idEntry = "login_email", focused = true)))
        assertEquals(listOf(0), form.usernames)
        assertEquals(What.USERNAME, form.what())
    }

    @Test
    fun ignoresSearchAndCodeFields() {
        val form = FormClassifier.classify(
            listOf(
                FieldInfo(0, inputType = text, hint = "Search"),
                FieldInfo(1, htmlType = "text", htmlAutocomplete = "one-time-code"),
            ),
        )
        assertTrue(form.empty)
    }

    @Test
    fun arabicFieldNames() {
        val form = FormClassifier.classify(
            listOf(
                FieldInfo(0, inputType = text, hint = "اسم المستخدم"),
                FieldInfo(1, inputType = text, hint = "كلمة المرور"),
            ),
        )
        assertEquals(listOf(0), form.usernames)
        assertEquals(listOf(1), form.passwords)
    }
}
