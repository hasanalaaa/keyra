package app.keyra.android

import android.app.Activity
import android.os.Bundle
import android.text.InputType
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout

/** A plain native login form in another package, to drive Keyra's autofill service by hand. */
class LoginFormActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val user = EditText(this).apply {
            hint = "Username"
            setAutofillHints(View.AUTOFILL_HINT_USERNAME)
            inputType = InputType.TYPE_CLASS_TEXT
        }
        val password = EditText(this).apply {
            hint = "Password"
            setAutofillHints(View.AUTOFILL_HINT_PASSWORD)
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
        }
        val submit = Button(this).apply {
            text = "Sign in"
            setOnClickListener { finish() } // ends the autofill session → save request
        }
        setContentView(LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(48, 200, 48, 48)
            addView(user)
            addView(password)
            addView(submit)
        })
    }
}
