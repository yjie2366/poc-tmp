from .app import create_app
import secrets
import os

# can pass config dictionary
app = create_app({
	"SECRET_KEY": secrets.token_hex(),
	"SQLALCHEMY_DATABASE_URI": 'sqlite:///auth.sqlite',
	'SQLALCHEMY_TRACK_MODIFICATIONS': False,
})
