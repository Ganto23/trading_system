import React, { useState } from "react";
import Dashboard from "./Dashboard";

export default function App() {
  const [token, setToken] = useState("");
  const [authenticated, setAuthenticated] = useState(false);

  return (
    <div className="min-h-screen bg-gray-100 flex flex-col items-center justify-center">
      {!authenticated ? (
        <div className="bg-white p-8 rounded shadow w-full max-w-sm">
          <h1 className="text-2xl font-bold mb-4 text-center">Trading Dashboard Login</h1>
          <input
            type="text"
            className="border p-2 w-full mb-4"
            placeholder="Enter API Token"
            value={token}
            onChange={e => setToken(e.target.value)}
          />
          <button
            className="bg-blue-600 text-white px-4 py-2 rounded w-full"
            onClick={() => setAuthenticated(!!token)}
            disabled={!token}
          >
            Authenticate
          </button>
        </div>
      ) : (
        <Dashboard token={token} />
      )}
    </div>
  );
}
