
import { Router } from "express";

export const tickersRouter = Router();

tickersRouter.get("/", async (req, res) => {    
    res.json([{
    "firstPrice": "72.875",
    "high": "72.875",
    "lastPrice": "72.857",
    "low": "72.857",
    "priceChange": "-0.018",
    "priceChangePercent": "-0.000247",
    "quoteVolume": "0",
    "symbol": "TATA_INR",
    "trades": "0",
    "volume": "0"
  }]);
});