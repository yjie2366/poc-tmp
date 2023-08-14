import json
import time
#import ssl

from werkzeug.security import gen_salt
from .models import User, db, OAuth2Client
from .oauth2 import authorization
from flask import g, request, Blueprint, render_template
from flask import session, redirect, flash, url_for
from authlib.integrations.flask_oauth2 import current_token
from authlib.oauth2 import OAuth2Error
from werkzeug.security import check_password_hash, generate_password_hash

bp = Blueprint('auth', __name__, url_prefix='/auth')

def current_user():
    if 'id' in session:
        uid = session['id']
        return User.query.get(uid)
    return None

@bp.before_app_request
def load_user():
	user = current_user()
	if user is None:
		g.user = None
	else:
		g.user = user

# the endpoint name for the route defaults to the name of the view function if the 
# endpoint parameter isn't passed
@bp.route('/')
def index():
	return redirect(url_for(".authorize"))

@bp.route('/register', methods=["GET", "POST"])
def register():
	# GET
	if request.method == "GET":
		return render_template("auth/register.html")

	# POST
	if request.method == "POST":
		form = request.form
		username = form["username"]
		password = form["password"]
		error = None
	
		if not username or not password:
			error = "Client name and password cannot be empty" 
		elif User.query.filter_by(username=username).first():
			error = "Provided username already exists"
	
		if error is None:
			user = User(username=username, password=generate_password_hash(password))
		else:
			flash(error)
			return render_template("auth/register.html")
	
		db.session.add(user)
		db.session.commit()
	
	return redirect(url_for('.login'))

@bp.route('/authorize', methods=['GET', 'POST'])
def authorize():
	if request.method == 'GET':
		return render_template('auth/authorize.html')

	if request.method == 'POST':
		return redirect(url_for('.login'))

@bp.route('/login', methods=["GET", "POST"])
def login():
	if request.method == "POST":
		username = request.form.get("username")
		password = request.form.get("password")
		user = User.query.filter_by(username=username).first()
		error = None

		if not user or not check_password_hash(user.password, password):
			error = "Invalid username/password"
			flash(error)
			return render_template("auth/auth.html")

		session['id'] = user.id

		client_id = gen_salt(24)
		client_id_issued_at = int(time.time())
		client = OAuth2Client(client_id=client_id, 
			client_id_issued_at=client_id_issued_at,
			user_id=user.id,
		)

		# generate client info
		grant_types = "authorization_code"
		client_metadata = {
			"client_name": username,
			"client_uri": url_for(".redirect_token"),
			"grant_types": grant_types,
			"redirect_uris": url_for(".redirect_token"),
			"response_types": "code",
			"scope": "profile"
		}

		client.set_client_metadata(client_metadata)
		client.client_secret = gen_salt(48)

		db.session.add(client)
		db.session.commit()

		authcode_url = {
			"response_type": "code",
			"redirect_uri": url_for(".redirect_token"),
			"client_id": client_id,
			"scope": "profile",
		}

		return redirect(url_for(".auth_code", **authcode_url), code=307)

	return render_template("auth/auth.html")


@bp.route('/auth_code', methods=["POST"])
def auth_code():
	user = current_user()
	if user is None and "username" in request.form:
		username = request.form.get("username")
		user = User.query.filter_by(username=username).first()

	#grant = authorization.get_consent_grant(end_user=user)
	auth_code_response = authorization.create_authorization_response(grant_user=user)

	return auth_code_response


@bp.route('/redirect_token', methods=['GET'])
def redirect_token():
	user = current_user()

	if request.method == "GET":
		auth_code = request.args.get("code")
		auth_client = OAuth2Client.query.filter_by(user_id=user.id).first()
		client_data = auth_client.client_metadata

		client_id = auth_client.client_id 
		client_secret = auth_client.client_secret
	
		client_data = {
			"client_id": client_id,
			"client_secret": client_secret,
			"grant_type": client_data["grant_types"],
			"scope": client_data["scope"],
			"code": auth_code
		}
		
		return client_data

@bp.route('/token', methods=["POST"])
def token():
	return authorization.create_token_response()

@bp.route('/logout')
def logout():
	session.clear()
	return redirect('/')
