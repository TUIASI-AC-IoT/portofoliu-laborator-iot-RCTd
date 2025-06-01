import os  
import uuid  
from flask import Flask, request, jsonify, abort, send_from_directory  
from werkzeug.utils import secure_filename  
  
DATA_DIR = os.environ.get("FILES_DIR", "data")   # directory managed by the service  
os.makedirs(DATA_DIR, exist_ok=True)             # create it if it does not exist  
  
app = Flask(__name__)  
  
def _safe_name(name: str) -> str:  
    """  
    Return a filename stripped of any path components  
    to prevent directory-traversal attacks.  
    """  
    return secure_filename(os.path.basename(name))  
  
  
def _file_path(name: str) -> str:  
    return os.path.join(DATA_DIR, _safe_name(name))  
  
  
def _exists(name: str) -> bool:  
    return os.path.isfile(_file_path(name))  
  
  
@app.route("/files", methods=["GET"])  
def list_files():  
    files = sorted(f for f in os.listdir(DATA_DIR) if os.path.isfile(_file_path(f)))  
    return jsonify({"files": files}), 200  
  
  
@app.route("/files/<string:filename>", methods=["GET"])  
def get_file(filename):  
    if not _exists(filename):  
        abort(404, description="File not found")  
    return send_from_directory(DATA_DIR, _safe_name(filename)), 200  
  
  
@app.route("/files/<string:filename>", methods=["POST"])  
def create_named_file(filename):  
    if _exists(filename):  
        abort(409, description="File already exists")  
  
    data = request.get_json(silent=True)  
    if not data or "content" not in data:  
        abort(400, description="JSON body with a 'content' field required")  
  
    with open(_file_path(filename), "w", encoding="utf-8") as f:  
        f.write(data["content"])  
  
    return jsonify({"message": "File created", "filename": _safe_name(filename)}), 201  
  
  
@app.route("/files", methods=["POST"])  
def create_auto_file():  
    data = request.get_json(silent=True)  
    if not data or "content" not in data:  
        abort(400, description="JSON body with a 'content' field required")  
  
    name = data.get("filename")  
    if name:  
        name = _safe_name(name)  
        if _exists(name):  
            abort(409, description="File already exists")  
    else:  
        name = f"{uuid.uuid4().hex}.txt"  
  
    with open(_file_path(name), "w", encoding="utf-8") as f:  
        f.write(data["content"])  
  
    return jsonify({"message": "File created", "filename": name}), 201  
  
  
@app.route("/files/<string:filename>", methods=["DELETE"])  
def delete_file(filename):  
    if not _exists(filename):  
        abort(404, description="File not found")  
  
    os.remove(_file_path(filename))  
    return jsonify({"message": "File deleted", "filename": _safe_name(filename)}), 200  
  
  
@app.route("/files/<string:filename>", methods=["PUT"])  
def update_file(filename):  
    if not _exists(filename):  
        abort(404, description="File not found")  
  
    data = request.get_json(silent=True)  
    if not data or "content" not in data:  
        abort(400, description="JSON body with a 'content' field required")  
  
    with open(_file_path(filename), "w", encoding="utf-8") as f:  
        f.write(data["content"])  
  
    return jsonify({"message": "File updated", "filename": _safe_name(filename)}), 200  
  
  
if __name__ == "__main__":   
    app.run(host="0.0.0.0", port=5000, debug=True)  
