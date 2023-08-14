import os
from flask import Flask
from .models import db
from .oauth2 import config_oauth
from .auth import bp

def create_app(config=None):
	app = Flask(__name__, instance_relative_config=True)

	if config is not None:
		if isinstance(config, dict):
			app.config.update(config)
		elif config.endswith('.py'):
			app.config.from_pyfile(config)

#	app.config.from_mapping(
#		SECRET_KEY=secrets.token_hex(),
#		DATABASE=os.path.join(app.instance_path, "auth.sqlite"),
#	)

	# ensure the instance folder exists
	try:
		os.makedirs(app.instance_path)
	except OSError:
		pass
	
	@app.before_first_request
	def create_db():
		db.create_all()

	db.init_app(app)
	config_oauth(app)

	app.register_blueprint(bp)

	app.add_url_rule("/", endpoint="auth.index")

	return app

