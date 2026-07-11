import { Router } from "express";

export const tradesRouter = Router();

tradesRouter.get("/", async (req, res) => {
    const { market } = req.query;
    // get from DB
    res.json([{
        "id":381352508,"isBuyerMaker":false,"price":"77.93","quantity":"5.09","quoteQuantity":"396.6637","timestamp":1783808842437
    }]);
})
