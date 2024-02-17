import requests
import re
from requests.auth import HTTPBasicAuth
from authlib.jose import jwt

home_endpoint = "http://127.0.0.1:5000"
auth_endpoint = "http://127.0.0.1:5000/oauth/authorize"
token_endpoint = "http://127.0.0.1:5000/oauth/token"
api_endpoint = "http://127.0.0.1:5000/api/me"

client_id = "9qJJpjd4Ei4AwhzXhlLmFU1D"
client_secret = "jWSMesAdn9QyaGpBZrhIDgTDwU8k9xIfqaBuuOdvn8LD9V8o"

client_basic_auth = HTTPBasicAuth(client_id, client_secret)

scope = "profile"

#code_verifier = generate_token(48)
auth_params = { "response_type": "code",
	"client_id": client_id,
	"scope": scope,
#	"code_challenge": code_verifier,
#	"code_challenge_method": "S256"
}

#client_id = "mWERpQjjB8v9tqUJCXhT195O"
login_val = { "username": "test" }
confirm_val = { "confirm": "yes" }
# default client authentication is 'client_secret_basic'
#client = OAuth2Session(client_id, client_secret, authorization_endpoint=auth_endpoint, token_endpoint=token_endpoint, scope="profile", code_challenge_method='S256')
#uri, state = client.create_authorization_url(auth_endpoint, code_verifier=code_verifier)

# need resource owner authorization
auth_session = requests.Session()
login = auth_session.post(home_endpoint, json=login_val)
response = auth_session.post(auth_endpoint, params=auth_params, json=confirm_val)
print(response.url)

# [] indicates a set of characters
# special characters lose their special meaning inside [] sets
# () matches regex inside the parenthesis, and indicates the start/end of a group
re_code = re.compile(r'code=([^&]+)&*')
re_state = re.compile(r'state=([^&]+)&*')
auth_code = re_code.findall(response.url)
state = re_state.findall(response.url)
#print(auth_code)
#print(state)

#token_session = requests.Session()
token_param = { "grant_type": "authorization_code",
	"scope": scope,
	"code": auth_code,
#	"code_verifier": code_verifier,
}
# should send parameters using the content-type 'application/x-www-form-urlencoded'
# if no auth: the server will return 'invalid client' error
token_resp = requests.post(token_endpoint, auth=client_basic_auth, data=token_param)
access_token = token_resp.json()['access_token']

token_header = { "Authorization": f"Bearer {access_token}" }
#print(token_header)
api_resp = requests.get(api_endpoint, headers=token_header)
#print(api_resp.text)

pubkey_endpoint = "http://127.0.0.1:5000/pubkey"
public_key = requests.get(pubkey_endpoint).json()
claims = jwt.decode(access_token, public_key['pubkey'])
print(claims, type(claims))
print(claims.header)
claims.validate()
